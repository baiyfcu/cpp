/**
 * redis_client.cpp
 *
 *  Created on: 2016-09-08
 *      Author: antigloss
 */

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "redis_client.h"

using namespace std;

namespace ant {

static string kSetOpErrMsg[RedisClient::kSetOpCnt] = {
		"Unknow error", "Key already exist", "Key not exist"
};

//----------------------------------------------------------
// Public Methods
//----------------------------------------------------------
RedisClient::RedisClient(const string& ip_port)
{
	string::size_type pos = ip_port.find_last_of(':');
	if (pos == string::npos) {
		throw runtime_error(string("Invalid ip_port [") + ip_port + "]");
	}

	m_ip_port = ip_port;
	m_ip.assign(ip_port, 0, pos);

	++pos;
	if (pos < ip_port.size()) {
		m_port = atoi(ip_port.substr(pos).c_str());
		if (m_port <= 0 || m_port > 65535) {
			throw runtime_error(string("Invalid ip_port [") + ip_port + "]");
		}
	} else {
		m_port = 6379;
	}

	alloc_context();
}

#ifdef CHECK_REPLY
#undef CHECK_REPLY
#endif
#define CHECK_REPLY(reply_, expectedReplyType_) \
		do { \
			if (!reply_) { \
				return false; \
			} \
			switch (reply_->type) { \
			case expectedReplyType_: \
				break; \
			case REDIS_REPLY_ERROR: \
				set_errmsg(reply_->str); \
				return false; \
			default: \
				set_errmsg("Unexpected reply type ", reply_->type); \
				return false; \
			} \
		} while (0)

bool RedisClient::select_db(int dbidx)
{
	ScopedReplyPointer reply = exec("SELECT %d", dbidx);
	CHECK_REPLY(reply, REDIS_REPLY_STATUS);
	return true;
}

bool RedisClient::expire(const std::string& key, unsigned int seconds)
{
	ScopedReplyPointer reply = exec("EXPIRE %b %u", key.c_str(),
									static_cast<size_t>(key.size()), seconds);
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	if (reply->integer == 1) {
		return true;
	}

	set_errmsg("Key does not exist or the timeout could not be set. reply->integer is ", reply->integer);
	return false;
}

bool RedisClient::expire_at(const std::string& key, time_t expired_tm)
{
	ScopedReplyPointer reply = exec("EXPIREAT %b %lld", key.c_str(),
									static_cast<size_t>(key.size()),
									static_cast<long long>(expired_tm));
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	if (reply->integer == 1) {
		return true;
	}

	set_errmsg("Key does not exist or the timeout could not be set. reply->integer is ", reply->integer);
	return false;
}

bool RedisClient::ttl(const std::string& key, long long& ttl)
{
	ScopedReplyPointer reply = exec("TTL %b", key.c_str(), static_cast<size_t>(key.size()));
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	ttl = reply->integer;
	return true;
}

bool RedisClient::set(const string& key, const string& val, const ExpirationTime* expire_tm, SetOpType op_type)
{
	ScopedReplyPointer reply;
	if (expire_tm) {
		long long ttl = expire_tm->remaining_seconds();
		if (ttl <= 0) {
			set_errmsg("Invalid expire time");
			return false;
		}

		switch (op_type) {
		case kSetAnyhow:
			reply = exec("SET %b %b EX %lld", key.c_str(), static_cast<size_t>(key.size()),
							val.c_str(), static_cast<size_t>(val.size()), ttl);
			break;
		case kSetIfNotExist:
			reply = exec("SET %b %b EX %lld NX", key.c_str(), static_cast<size_t>(key.size()),
							val.c_str(), static_cast<size_t>(val.size()), ttl);
			break;
		case kSetIfExist:
			reply = exec("SET %b %b EX %lld XX", key.c_str(), static_cast<size_t>(key.size()),
							val.c_str(), static_cast<size_t>(val.size()), ttl);
			break;
		default:
			set_errmsg("Unsupported op_type ", op_type);
			break;
		}
	} else {
		switch (op_type) {
		case kSetAnyhow:
			reply = exec("SET %b %b", key.c_str(), static_cast<size_t>(key.size()),
							val.c_str(), static_cast<size_t>(val.size()));
			break;
		case kSetIfNotExist:
			reply = exec("SET %b %b NX", key.c_str(), static_cast<size_t>(key.size()),
							val.c_str(), static_cast<size_t>(val.size()));
			break;
		case kSetIfExist:
			reply = exec("SET %b %b XX", key.c_str(), static_cast<size_t>(key.size()),
							val.c_str(), static_cast<size_t>(val.size()));
			break;
		default:
			set_errmsg("Unsupported op_type ", op_type);
			break;
		}
	}

	if (!reply) {
		return false;
	}

	switch (reply->type) {
	case REDIS_REPLY_STATUS:
		return true;
	case REDIS_REPLY_NIL:
		set_errmsg(kSetOpErrMsg[op_type]);
		return false;
	case REDIS_REPLY_ERROR:
		set_errmsg(reply->str);
		return false;
	default:
		set_errmsg("Unexpected reply type ", reply->type);
		return false;
	}
}

bool RedisClient::get(const std::string& key, std::string& val, bool* key_exists)
{
	ScopedReplyPointer reply = exec("GET %b", key.c_str(), static_cast<size_t>(key.size()));
	if (!reply) {
		return false;
	}

	switch (reply->type) {
	case REDIS_REPLY_STRING:
		val.assign(reply->str, reply->len);
		if (key_exists) {
			*key_exists = true;
		}
		return true;
	case REDIS_REPLY_NIL:
		val = "";
		if (key_exists) {
			*key_exists = false;
		}
		return true;
	case REDIS_REPLY_ERROR:
		set_errmsg(reply->str);
		return false;
	default:
		set_errmsg("Unexpected reply type ", reply->type);
		return false;
	}
}

bool RedisClient::sadd(const string& key, const vector<string>& vals, long long* cnt)
{
	ScopedReplyPointer reply = execv("SADD", &key, &vals);
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	if (cnt) {
		*cnt = reply->integer;
	}
	return true;
}

bool RedisClient::scard(const std::string& key, long long& cnt)
{
	ScopedReplyPointer reply = exec("SCARD %b", key.c_str(), static_cast<size_t>(key.size()));
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	cnt = reply->integer;
	return true;
}

bool RedisClient::sdiff(const vector<string>& keys, vector<string>& result)
{
	ScopedReplyPointer reply = execv("SDIFF", 0, &keys);
	CHECK_REPLY(reply, REDIS_REPLY_ARRAY);
	arr_reply_to_vector(reply->element, reply->elements, result);
	return true;
}

bool RedisClient::sdiff_store(const string& dest, const vector<string>& keys, long long* cnt)
{
	ScopedReplyPointer reply = execv("SDIFFSTORE", &dest, &keys);
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	if (cnt) {
		*cnt = reply->integer;
	}
	return true;
}

bool RedisClient::sinter(const vector<string>& keys, vector<string>& result)
{
	ScopedReplyPointer reply = execv("SINTER", 0, &keys);
	CHECK_REPLY(reply, REDIS_REPLY_ARRAY);
	arr_reply_to_vector(reply->element, reply->elements, result);
	return true;
}

bool RedisClient::sinter_store(const string& dest, const vector<string>& keys, long long* cnt)
{
	ScopedReplyPointer reply = execv("SINTERSTORE", &dest, &keys);
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	if (cnt) {
		*cnt = reply->integer;
	}
	return true;
}

bool RedisClient::sismember(const std::string& key, const std::string& val, bool& is_member)
{
	ScopedReplyPointer reply = exec("SISMEMBER %b %b", key.c_str(), static_cast<size_t>(key.size()),
									val.c_str(), static_cast<size_t>(val.size()));
	CHECK_REPLY(reply, REDIS_REPLY_INTEGER);
	is_member = reply->integer;
	return true;
}

// TODO -----  Set Ops ----------------------------------

bool RedisClient::hget(const string& key, map<string, string>& fields)
{
	ScopedReplyPointer reply = execm("HMGET", key, fields);
	CHECK_REPLY(reply, REDIS_REPLY_ARRAY);

	if (reply->elements != fields.size()) {
		set_errmsg("Invalid number of elements returned! Expected ",
					fields.size(), ", Returned ", reply->elements);
		return false;
	}

	size_t n = 0;
	for (map<string, string>::iterator it = fields.begin(); it != fields.end(); ++it) {
		redisReply* r = reply->element[n];
		switch (r->type) {
		case REDIS_REPLY_STRING:
			it->second = r->str;
			break;
		case REDIS_REPLY_NIL:
			it->second = "";
			break;
		case REDIS_REPLY_ERROR:
			set_errmsg(reply->str);
			return false;
		default:
			set_errmsg("Unexpected reply type ", reply->type);
			return false;
		}
		++n;
	}
	return true;
}

bool RedisClient::hset(const std::string& key, const std::map<std::string, std::string>& fields)
{
	ScopedReplyPointer reply = execm("HMSET", key, fields, true);
	CHECK_REPLY(reply, REDIS_REPLY_STATUS);
	return true;
}

//----------------------------------------------------------
// Private Methods
//----------------------------------------------------------
bool RedisClient::alloc_context()
{
	timeval timeout = { 2, 0 };
	m_context = redisConnectWithTimeout(m_ip.c_str(), m_port, timeout);
	if (m_context && (m_context->err == 0)) {
		return true;
	}

	if (m_context) {
		set_errmsg("Failed to connect to ", m_ip_port, ": ", m_context->errstr, " (", m_context->err, ')');
		free_context();
	} else {
		set_errmsg("Failed to connect to ", m_ip_port, ": Cannot allocate redisContext");
	}

	return false;
}

redisReply* RedisClient::exec(const char* fmt, ...)
{
	if (!has_context() && !alloc_context()) {
		return 0;
	}

	va_list ap;
	va_start(ap, fmt);
	redisReply* reply = reinterpret_cast<redisReply*>(redisvCommand(m_context, fmt, ap));
	va_end(ap);

	if (!reply) {
		set_errmsg(m_context->errstr, " (", m_context->err, ')');
		free_context();
	}
	return reply;
}

void RedisClient::arr_reply_to_vector(redisReply* const replies[], size_t reply_num, vector<string>& result)
{
	result.clear();
	result.reserve(reply_num);
	for (size_t i = 0; i != reply_num; ++i) {
		result.emplace_back(string(replies[i]->str, replies[i]->len));
	}
}

}
