/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// REDIS implementation of the storage API of the MaxScale cache filter
//
// Documentation:
// - /usr/include/hiredis
// - https://github.com/redis/hiredis/blob/master/README.md
// - https://redis.io/commands
//

#define MXS_MODULE_NAME "storage_redis"
#include "redisstorage.hh"
#include <hiredis/hiredis.h>
#include <maxscale/threadpool.hh>

using std::shared_ptr;
using std::string;
using std::vector;

namespace
{

const char* redis_type_to_string(int type)
{
    switch (type)
    {
    case REDIS_REPLY_ARRAY:
        return "ARRAY";

    case REDIS_REPLY_ERROR:
        return "ERROR";

    case REDIS_REPLY_INTEGER:
        return "INTEGER";

    case REDIS_REPLY_NIL:
        return "NIL";

    case REDIS_REPLY_STATUS:
        return "STATUS";

    case REDIS_REPLY_STRING:
        return "STRING";
    }

    return "UNKNOWN";
}

string redis_error_to_string(int err)
{
    switch (err)
    {
    case REDIS_OK:
        return "no error";

    case REDIS_ERR_IO:
        {
            int e = errno;
            string s("redis I/O error: ");
            s += mxb_strerror(e);
        }
        break;

    case REDIS_ERR_EOF:
        return "server closed the connection";

    case REDIS_ERR_PROTOCOL:
        return "error while parsing the protocol";

    case REDIS_ERR_OTHER:
        return "unspecified error (possibly unresolved hostname)";

    case REDIS_ERR:
        return "general error";
    }

    return "unknown error";
}

class RedisToken : public std::enable_shared_from_this<RedisToken>,
                   public Storage::Token
{
public:
    ~RedisToken()
    {
        redisFree(m_pRedis);
    }

    shared_ptr<RedisToken> get_shared()
    {
        return shared_from_this();
    }

    static bool create(const string& host, int port, uint32_t ttl, shared_ptr<Storage::Token>* psToken)
    {
        bool rv = false;
        redisContext* pRedis = redisConnect(host.c_str(), port);

        if (pRedis)
        {
            RedisToken* pToken = new (std::nothrow) RedisToken(pRedis, ttl);

            if (pToken)
            {
                psToken->reset(pToken);
                rv = true;
            }
            else
            {
                redisFree(pRedis);
            }
        }
        else
        {
            MXS_ERROR("Could not create redis handle, are the arguments '%s:%d' valid?",
                      host.c_str(), port);
        }

        return rv;
    }

    cache_result_t get_value(const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue,
                             std::function<void (cache_result_t, GWBUF*)> cb)
    {
        vector<char> rkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, cb] () {
                void* pReply = redisCommand(sThis->m_pRedis, "GET %b", rkey.data(), rkey.size());
                redisReply* pRrv = static_cast<redisReply*>(pReply);

                GWBUF* pValue = nullptr;
                cache_result_t rv = CACHE_RESULT_ERROR;

                if (pRrv)
                {
                    switch (pRrv->type)
                    {
                    case REDIS_REPLY_STRING:
                        pValue = gwbuf_alloc_and_load(pRrv->len, pRrv->str);
                        rv = CACHE_RESULT_OK;
                        break;

                    case REDIS_REPLY_NIL:
                        rv = CACHE_RESULT_NOT_FOUND;
                        break;

                    default:
                        MXS_WARNING("Unexpected redis redis return type (%s) received.",
                                    redis_type_to_string(pRrv->type));
                    }

                    freeReplyObject(pRrv);
                }
                else
                {
                    MXS_WARNING("Fatally failed when fetching cached value from redis: %s",
                                sThis->m_pRedis->errstr);
                }

                sThis->m_pWorker->execute([sThis, rv, pValue, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv, pValue);
                        }
                        else
                        {
                            gwbuf_free(pValue);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

    cache_result_t put_value(const CacheKey& key,
                             const vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             std::function<void (cache_result_t)> cb)
    {
        vector<char> rkey = key.to_vector();

        GWBUF* pClone = gwbuf_clone(const_cast<GWBUF*>(pValue));
        MXS_ABORT_IF_NULL(pClone);

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, invalidation_words, pClone, cb]() {
                int n = invalidation_words.size();
                if (n != 0)
                {
                    // 'rkey' is the key that identifies the value. So, we store it to
                    // a redis hash that is identified by each invalidation word, aka
                    // the table name.

                    for (int i = 0; i < n; ++i)
                    {
                        const char* pHash = invalidation_words[i].c_str();
                        int hash_len = invalidation_words[i].length();
                        const char* pField = rkey.data();
                        int field_len = rkey.size();

                        // redisAppendCommand can only fail if we run out of memory
                        // or if the format string is broken.
                        MXB_AT_DEBUG(int rc =) redisAppendCommand(sThis->m_pRedis, "HSET %b %b \"1\"",
                                                                  pHash, hash_len,
                                                                  pField, field_len);
                        mxb_assert(rc == REDIS_OK);
                    }
                }

                // Then the actual value is stored.
                MXB_AT_DEBUG(int rc =) redisAppendCommand(sThis->m_pRedis, "SET %b %b PX %u",
                                                          rkey.data(), rkey.size(),
                                                          reinterpret_cast<const char*>(GWBUF_DATA(pClone)),
                                                          GWBUF_LENGTH(pClone),
                                                          sThis->m_ttl);
                mxb_assert(rc == REDIS_OK);

                ++n;

                cache_result_t rv = CACHE_RESULT_OK;

                // First we handle the replies to the "HSET" commands.
                for (int i = 0; i < n - 1; ++i)
                {
                    void* pV;
                    int rc = redisGetReply(sThis->m_pRedis, &pV);

                    if (rc == REDIS_OK)
                    {
                        redisReply* pRrv = static_cast<redisReply*>(pV);
                        mxb_assert(pRrv->type == REDIS_REPLY_INTEGER);
                        mxb_assert(pRrv->integer != 1 && pRrv->integer != 0); // If 0, the key existed already.

                        freeReplyObject(pRrv);
                    }
                    else
                    {
                        MXS_ERROR("Could not read redis reply for hash update for '%s', the cache is "
                                  "now in an inconsistent state: %s, %s",
                                  invalidation_words[i].c_str(),
                                  redis_error_to_string(rc).c_str(),
                                  sThis->m_pRedis->errstr);
                        rv = CACHE_RESULT_ERROR;
                    }
                }

                if (rv == CACHE_RESULT_OK)
                {
                    void* pV;
                    int rc = redisGetReply(sThis->m_pRedis, &pV);

                    if (rc == REDIS_OK)
                    {
                        redisReply* pRrv = static_cast<redisReply*>(pV);
                        mxb_assert(pRrv->type == REDIS_REPLY_STATUS);

                        if (strncmp(pRrv->str, "OK", 2) == 0)
                        {
                            rv = CACHE_RESULT_OK;
                        }
                        else
                        {
                            MXS_ERROR("Failed when storing cache value to redis, expected 'OK' but "
                                      "received '%s'.", pRrv->str);
                            rv = CACHE_RESULT_ERROR;
                        }

                        freeReplyObject(pRrv);
                    }
                    else
                    {
                        MXS_WARNING("Failed fatally when storing cache value to redis: %s, %s",
                                    redis_error_to_string(rc).c_str(),
                                    sThis->m_pRedis->errstr);
                    }
                }

                sThis->m_pWorker->execute([sThis, pClone, rv, cb]() {
                        // TODO: So as not to trigger an assert in buffer.cc, we need to delete
                        // TODO: the gwbuf in the same worker where it was allocated. This means
                        // TODO: that potentially a very large buffer is kept around for longer
                        // TODO: than necessary. Perhaps time to stop tracking buffer ownership.
                        gwbuf_free(pClone);

                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

    cache_result_t del_value(const CacheKey& key,
                             std::function<void (cache_result_t)> cb)
    {
        vector<char> rkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, cb] () {
                void* pReply = redisCommand(sThis->m_pRedis, "DEL %b", rkey.data(), rkey.size());

                redisReply* pRrv = static_cast<redisReply*>(pReply);

                cache_result_t rv = CACHE_RESULT_ERROR;

                if (pRrv)
                {
                    if (pRrv->type == REDIS_REPLY_INTEGER)
                    {
                        switch (pRrv->integer)
                        {
                        case 0:
                            rv = CACHE_RESULT_NOT_FOUND;
                            break;

                        default:
                            MXS_WARNING("Unexpected number of values - %lld - deleted with one key,",
                                        pRrv->integer);
                            /* FLOWTHROUGH */
                        case 1:
                            rv = CACHE_RESULT_OK;
                            break;
                        }
                    }
                    else
                    {
                        MXS_WARNING("Unexpected redis return type (%s) received.",
                                    redis_type_to_string(pRrv->type));
                    }

                    freeReplyObject(pRrv);
                }
                else
                {
                    MXS_WARNING("Failed fatally when deleting cached value from redis: %s",
                                sThis->m_pRedis->errstr);
                }

                sThis->m_pWorker->execute([sThis, rv, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

    cache_result_t invalidate(const vector<string>& words,
                              std::function<void (cache_result_t)> cb)
    {
        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, words, cb] () {
                int n = words.size();
                if (n != 0)
                {
                    // For each invalidation word (aka table name) we fetch all
                    // keys.
                    for (int i = 0; i < n; ++i)
                    {
                        const char* pHash = words[i].c_str();
                        int hash_len = words[i].length();

                        // redisAppendCommand can only fail if we run out of memory
                        // or if the format string is broken.
                        MXB_AT_DEBUG(int rc =) redisAppendCommand(sThis->m_pRedis, "HGETALL %b",
                                                                  pHash, hash_len);
                        mxb_assert(rc == REDIS_OK);
                    }
                }

                // Then we iterate over the replies and build one DEL command for
                // deleting all values and one HDEL for each invalidation word for
                // deleting the keys of each word.

                vector<redisReply*> to_free;

                vector<vector<char*>> hdel_argvs;
                vector<vector<size_t>> hdel_argvlens;

                vector<char*> del_argv;
                vector<size_t> del_argvlen;

                del_argv.push_back(const_cast<char*>("DEL"));
                del_argvlen.push_back(3);

                for (int i = 0; i < n; ++i)
                {
                    void* pV;
                    int rc = redisGetReply(sThis->m_pRedis, &pV);

                    if (rc == REDIS_OK)
                    {
                        redisReply* pRrv = static_cast<redisReply*>(pV);

                        if (pRrv->type == REDIS_REPLY_ARRAY)
                        {
                            vector<char*> hdel_argv;
                            vector<size_t> hdel_argvlen;

                            hdel_argv.push_back(const_cast<char*>("HDEL"));
                            hdel_argvlen.push_back(4);

                            hdel_argv.push_back(const_cast<char*>(words[i].c_str()));
                            hdel_argvlen.push_back(words[i].length());

                            // 'j = j + 2' since key/value pairs are returned.
                            for (size_t j = 0; j < pRrv->elements; j = j + 2)
                            {
                                redisReply* pElement = pRrv->element[j];

                                if (pElement->type == REDIS_REPLY_STRING)
                                {
                                    del_argv.push_back(pElement->str);
                                    del_argvlen.push_back(pElement->len);

                                    hdel_argv.push_back(pElement->str);
                                    hdel_argvlen.push_back(pElement->len);
                                }
                                else
                                {
                                    MXS_ERROR("Unexpected type returned by redis: %s",
                                              redis_type_to_string(pElement->type));
                                }
                            }

                            hdel_argvs.push_back(std::move(hdel_argv));
                            hdel_argvlens.push_back(std::move(hdel_argvlen));
                        }

                        to_free.push_back(pRrv);
                    }
                    else
                    {
                        MXS_ERROR("Could not read redis reply for hash update for '%s': %s, %s",
                                  words[i].c_str(),
                                  redis_error_to_string(rc).c_str(),
                                  sThis->m_pRedis->errstr);
                    }
                }

                cache_result_t rv = CACHE_RESULT_OK;

                if (del_argv.size() > 1)
                {
                    // Delete all values, the DEL command.
                    const char** ppDel_argv = const_cast<const char**>(del_argv.data());
                    int rc = redisAppendCommandArgv(sThis->m_pRedis,
                                                    del_argv.size(),
                                                    ppDel_argv,
                                                    del_argvlen.data());
                    mxb_assert(rc == REDIS_OK);

                    for (size_t i = 0; i < hdel_argvs.size(); ++i)
                    {
                        // Delete keys related to a particular table, the HDEL commands.
                        const vector<char*>& hdel_argv = hdel_argvs[i];
                        const vector<size_t>& hdel_argvlen = hdel_argvlens[i];

                        if (hdel_argv.size() > 2)
                        {
                            const char** ppHdel_argv = const_cast<const char**>(hdel_argv.data());
                            MXB_AT_DEBUG(int rc =) redisAppendCommandArgv(sThis->m_pRedis,
                                                                          hdel_argv.size(),
                                                                          ppHdel_argv,
                                                                          hdel_argvlen.data());
                            mxb_assert(rc == REDIS_OK);
                        }
                    }

                    // 1) When a value is stored, we first update the hash with the key and then save the value.
                    // 2) When invalidating, we first delete the value(s) and then delete the keys in the hash.
                    //
                    // Does this work? No, e.g:
                    //
                    // Client 1         Client 2
                    // -------------------------
                    // put_value()      invalidate()
                    //    update hash
                    //                  delete value
                    //    save value
                    //                  delete keys
                    //
                    // All actions need to be performed transactionally. Redis command MULTI/EXEC to be taken
                    // into use later.

                    // First the reply to the DEL command
                    void* pV;
                    rc = redisGetReply(sThis->m_pRedis, &pV);

                    if (rc == REDIS_OK)
                    {
                        redisReply* pRrv = static_cast<redisReply*>(pV);
                        mxb_assert(pRrv->type == REDIS_REPLY_INTEGER);

                        freeReplyObject(pRrv);

                        // Then the replies to the HDEL commands.
                        for (size_t i = 0; i < hdel_argvs.size() && rc == REDIS_OK; ++i)
                        {
                            void* pV;
                            int rc = redisGetReply(sThis->m_pRedis, &pV);

                            if (rc == REDIS_OK)
                            {
                                redisReply* pRrv = static_cast<redisReply*>(pV);
                                mxb_assert(pRrv->type == REDIS_REPLY_INTEGER);

                                freeReplyObject(pRrv);
                            }
                            else
                            {
                                MXS_ERROR("Could not read HDEL reply from redis, the cache is now "
                                          "in an unknown state: %s, %s",
                                          redis_error_to_string(rc).c_str(),
                                          sThis->m_pRedis->errstr);
                                rv = CACHE_RESULT_ERROR;
                            }
                        }
                    }
                    else
                    {
                        MXS_ERROR("Could not read DEL reply from redis, the cache is now "
                                  "in an unknown state: %s, %s",
                                  redis_error_to_string(rc).c_str(),
                                  sThis->m_pRedis->errstr);
                        rv = CACHE_RESULT_ERROR;
                    }
                }

                for (auto* pReply : to_free)
                {
                    freeReplyObject(pReply);
                }

                sThis->m_pWorker->execute([sThis, rv, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

private:
    RedisToken(redisContext* pRedis, uint32_t ttl)
        : m_pRedis(pRedis)
        , m_pWorker(mxb::Worker::get_current())
        , m_ttl(ttl)
    {
    }

private:
    redisContext* m_pRedis;
    mxb::Worker*  m_pWorker;
    uint32_t      m_ttl;
};

}


RedisStorage::RedisStorage(const string& name,
                           const Config& config,
                           const string& host,
                           int port)
    : m_name(name)
    , m_config(config)
    , m_host(host)
    , m_port(port)
{
    if (config.soft_ttl != config.hard_ttl)
    {
        MXS_WARNING("The storage storage_redis does not distinguish between "
                    "soft (%u ms) and hard ttl (%u ms). Hard ttl is used.",
                    config.soft_ttl, config.hard_ttl);
    }

    auto ms = config.hard_ttl;

    if (ms == 0)
    {
        m_ttl = 0;
    }
    else
    {
        m_ttl = config.hard_ttl;
    }
}

RedisStorage::~RedisStorage()
{
}

//static
bool RedisStorage::initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities)
{
    *pKind = CACHE_STORAGE_SHARED;
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT | CACHE_STORAGE_CAP_INVALIDATION);
    return true;
}

//static
void RedisStorage::finalize()
{
}

//static
RedisStorage* RedisStorage::create(const string& name,
                                   const Config& config,
                                   const std::string& arguments)
{
    RedisStorage* pStorage = nullptr;

    if (config.max_size != 0)
    {
        MXS_WARNING("The storage storage_redis does not support specifying "
                    "a maximum size of the cache storage.");
    }

    if (config.max_count != 0)
    {
        MXS_WARNING("The storage storage_redis does not support specifying "
                    "a maximum number of items in the cache storage.");
    }

    vector<string> vs = mxb::strtok(arguments, ":");

    if (vs.size() == 2)
    {
        string host = vs[0];
        int port;

        if (mxb::get_int(vs[1], &port) && port > 0)
        {
            pStorage = new (std::nothrow) RedisStorage(name, config, host, port);
        }
        else
        {
            MXS_ERROR("The provided arugments '%s' does not translate into a valid "
                      "host:port combination.", arguments.c_str());
        }
    }
    else
    {
        MXS_ERROR("storage_redis expects a `storage_options` argument of "
                  "HOST:PORT format: %s", arguments.c_str());
    }

    return pStorage;
}

bool RedisStorage::create_token(shared_ptr<Storage::Token>* psToken)
{
    return RedisToken::create(m_host, m_port, m_ttl, psToken);
}

void RedisStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

cache_result_t RedisStorage::get_info(uint32_t what, json_t** ppInfo) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_value(Storage::Token* pToken,
                                       const CacheKey& key,
                                       uint32_t flags,
                                       uint32_t soft_ttl,
                                       uint32_t hard_ttl,
                                       GWBUF** ppValue,
                                       std::function<void (cache_result_t, GWBUF*)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->get_value(key, flags, soft_ttl, hard_ttl, ppValue, cb);
}

cache_result_t RedisStorage::put_value(Token* pToken,
                                       const CacheKey& key,
                                       const vector<string>& invalidation_words,
                                       const GWBUF* pValue,
                                       std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->put_value(key, invalidation_words, pValue, cb);
}

cache_result_t RedisStorage::del_value(Token* pToken,
                                       const CacheKey& key,
                                       std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->del_value(key, cb);
}

cache_result_t RedisStorage::invalidate(Token* pToken,
                                        const vector<string>& words,
                                        std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->invalidate(words, cb);
}

cache_result_t RedisStorage::clear(Token* pToken)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_head(CacheKey* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_tail(CacheKey* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_ERROR;
}
