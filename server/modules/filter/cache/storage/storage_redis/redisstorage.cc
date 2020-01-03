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
        vector<char> mkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, mkey, cb] () {
                void* pReply = redisCommand(sThis->m_pRedis, "GET %b", mkey.data(), mkey.size());
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
        vector<char> mkey = key.to_vector();

        GWBUF* pClone = gwbuf_clone(const_cast<GWBUF*>(pValue));
        MXS_ABORT_IF_NULL(pClone);

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, mkey, pClone, cb]() {
                void* pReply = redisCommand(sThis->m_pRedis, "SET %b %b PX %u",
                                            mkey.data(), mkey.size(),
                                            reinterpret_cast<const char*>(GWBUF_DATA(pClone)),
                                            GWBUF_LENGTH(pClone),
                                            sThis->m_ttl);
                redisReply* pRrv = static_cast<redisReply*>(pReply);

                cache_result_t rv = CACHE_RESULT_ERROR;

                if (pRrv)
                {
                    if (pRrv->type == REDIS_REPLY_STATUS)
                    {
                        if (strncmp(pRrv->str, "OK", 2) == 0)
                        {
                            rv = CACHE_RESULT_OK;
                        }
                        else
                        {
                            MXS_WARNING("Failed when storing cache value to redis.");
                            rv = CACHE_RESULT_ERROR;
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
                    MXS_WARNING("Failed fatally when storing cache value to redis: %s",
                                sThis->m_pRedis->errstr);
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
        vector<char> mkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, mkey, cb] () {
                void* pReply = redisCommand(sThis->m_pRedis, "DEL %b", mkey.data(), mkey.size());

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
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT);
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

    if (config.invalidate != CACHE_INVALIDATE_NEVER)
    {
        MXS_ERROR("The storage storage_redis does not support invalidation.");
    }
    else
    {
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
    return CACHE_RESULT_ERROR;
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
