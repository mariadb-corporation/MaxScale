/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "memcachedstorage.hh"
#include <memory>
#include <libmemcached/memcached.h>
#include <libmemcached-1.0/strerror.h>
#include <maxbase/alloc.hh>
#include <maxbase/worker.hh>
#include <maxscale/config_common.hh>
#include <maxscale/threadpool.hh>
#include "../../cache.hh"

using std::map;
using std::shared_ptr;
using std::string;
using std::vector;

namespace
{

const char CN_MEMCACHED_MAX_VALUE_SIZE[] = "max_value_size";

const int DEFAULT_MEMCACHED_PORT = 11211;
const int DEFAULT_MAX_VALUE_SIZE = 1 * 1024 * 1024;

class MemcachedToken : public std::enable_shared_from_this<MemcachedToken>,
                       public Storage::Token
{
public:
    ~MemcachedToken()
    {
        memcached_free(m_pMemc);
    }

    std::shared_ptr<MemcachedToken> get_shared()
    {
        return shared_from_this();
    }

    static bool create(const MemcachedConfig* pConfig,
                       std::chrono::milliseconds timeout,
                       uint32_t soft_ttl,
                       uint32_t hard_ttl,
                       uint32_t mcd_ttl,
                       shared_ptr<Storage::Token>* psToken)
    {
        bool rv = false;

        string arguments;

        arguments += "--SERVER=";
        arguments += pConfig->host.address();
        arguments += ":";
        arguments += std::to_string(pConfig->host.port());

        arguments += " --CONNECT-TIMEOUT=";
        arguments += std::to_string(timeout.count());


        memcached_st* pMemc = memcached(arguments.c_str(), arguments.size());

        if (pMemc)
        {
            memcached_return_t mrv = memcached_behavior_set(pMemc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);

            if (memcached_success(mrv))
            {
                MemcachedToken* pToken = new (std::nothrow) MemcachedToken(pMemc, pConfig, timeout,
                                                                           soft_ttl, hard_ttl, mcd_ttl);

                if (pToken)
                {
                    psToken->reset(pToken);

                    // The call to connect() (-> get_shared() -> shared_from_this()) can be made only
                    // after the pointer has been stored in a shared_ptr.
                    pToken->connect();
                    rv = true;
                }
                else
                {
                    memcached_free(pMemc);
                }
            }
            else
            {
                MXB_ERROR("Could not turn on memcached binary protocol: %s",
                          memcached_strerror(pMemc, mrv));
                memcached_free(pMemc);
            }
        }
        else
        {
            MXB_ERROR("Could not create memcached handle using the arguments '%s'. "
                      "Is the host/port and timeout combination valid?",
                      arguments.c_str());
        }

        return rv;
    }

    cache_result_t get_value(const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF* pValue,
                             std::function<void (cache_result_t, GWBUF&&)> cb)
    {
        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_NOT_FOUND;
        }

        if (soft_ttl == CACHE_USE_CONFIG_TTL)
        {
            soft_ttl = m_soft_ttl;
        }

        if (hard_ttl == CACHE_USE_CONFIG_TTL)
        {
            hard_ttl = m_hard_ttl;
        }

        if (soft_ttl > hard_ttl)
        {
            soft_ttl = hard_ttl;
        }

        vector<char> mkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, flags, soft_ttl, hard_ttl, mkey, cb] () {
                size_t nData;
                uint32_t stored; // The store-time is stored as flags.
                memcached_return_t mrv;

                char* pData = memcached_get(sThis->m_pMemc, mkey.data(), mkey.size(), &nData, &stored, &mrv);

                GWBUF value;
                cache_result_t rv;

                if (memcached_success(mrv))
                {
                    if (pData)
                    {
                        uint32_t now = Cache::time_ms();

                        bool is_hard_stale = hard_ttl == 0 ? false : (now - stored > hard_ttl);
                        bool is_soft_stale = soft_ttl == 0 ? false : (now - stored > soft_ttl);
                        bool include_stale = ((flags & CACHE_FLAGS_INCLUDE_STALE) != 0);

                        if (is_hard_stale)
                        {
                            rv = CACHE_RESULT_NOT_FOUND | CACHE_RESULT_DISCARDED;
                        }
                        else if (!is_soft_stale || include_stale)
                        {
                            value = GWBUF(reinterpret_cast<const uint8_t*>(pData), nData);

                            rv = CACHE_RESULT_OK;

                            if (is_soft_stale)
                            {
                                rv |= CACHE_RESULT_STALE;
                            }
                        }
                        else
                        {
                            mxb_assert(is_soft_stale);
                            rv = CACHE_RESULT_NOT_FOUND | CACHE_RESULT_STALE;
                        }

                        MXB_FREE(pData);
                    }
                    else
                    {
                        // TODO: With the textual protocol you could get this; NULL returned but
                        // TODO: no error reported. Does not seem to be a problem with the binary
                        // TODO: protocol enabled.
                        MXB_WARNING("NULL value returned from memcached, but no error reported.");
                        rv = CACHE_RESULT_NOT_FOUND;
                    }
                }
                else
                {
                    switch (mrv)
                    {
                    case MEMCACHED_NOTFOUND:
                        rv = CACHE_RESULT_NOT_FOUND;
                        break;

                    default:
                        MXB_WARNING("Failed when fetching cached value from memcached: %s, %s",
                                    memcached_strerror(sThis->m_pMemc, mrv),
                                    memcached_last_error_message(sThis->m_pMemc));
                        rv = CACHE_RESULT_ERROR;
                    }
                }

                sThis->m_pWorker->execute([sThis, rv, sBuffer = std::make_shared<GWBUF>(std::move(value)), cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            if (rv == CACHE_RESULT_ERROR)
                            {
                                sThis->connection_broken();
                            }

                            cb(rv, std::move(*sBuffer));
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            }, "memcached-get");

        return CACHE_RESULT_PENDING;
    }

    cache_result_t put_value(const CacheKey& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF& value,
                             const std::function<void (cache_result_t)>& cb)
    {
        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_OK;
        }

        vector<char> mkey = key.to_vector();

        auto sClone = std::make_shared<GWBUF>(value.shallow_clone());

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, mkey, sClone, cb]() {
                const uint32_t flags = Cache::time_ms();
                memcached_return_t mrv = memcached_set(sThis->m_pMemc, mkey.data(), mkey.size(),
                                                       reinterpret_cast<const char*>(sClone->data()),
                                                       sClone->length(), sThis->m_mcd_ttl, flags);
                cache_result_t rv;

                if (memcached_success(mrv))
                {
                    rv = CACHE_RESULT_OK;
                }
                else
                {
                    MXB_WARNING("Failed when storing cache value to memcached: %s, %s",
                                memcached_strerror(sThis->m_pMemc, mrv),
                                memcached_last_error_message(sThis->m_pMemc));
                    rv = CACHE_RESULT_ERROR;
                }

                sThis->m_pWorker->execute([sThis, rv, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            if (rv == CACHE_RESULT_ERROR)
                            {
                                sThis->connection_broken();
                            }

                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            }, "memcached-put");

        return CACHE_RESULT_PENDING;
    }

    cache_result_t del_value(const CacheKey& key,
                             const std::function<void (cache_result_t)>& cb)
    {
        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_NOT_FOUND;
        }

        vector<char> mkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, mkey, cb] () {
                memcached_return_t mrv = memcached_delete(sThis->m_pMemc, mkey.data(), mkey.size(), 0);

                cache_result_t rv;

                if (memcached_success(mrv))
                {
                    rv = CACHE_RESULT_OK;
                }
                else
                {
                    MXB_WARNING("Failed when deleting cached value from memcached: %s, %s",
                                memcached_strerror(sThis->m_pMemc, mrv),
                                memcached_last_error_message(sThis->m_pMemc));
                    rv = CACHE_RESULT_ERROR;
                }

                sThis->m_pWorker->execute([sThis, rv, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            if (rv == CACHE_RESULT_ERROR)
                            {
                                sThis->connection_broken();
                            }

                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            }, "memcached-del");

        return CACHE_RESULT_PENDING;
    }

private:
    MemcachedToken(memcached_st* pMemc,
                   const MemcachedConfig* pConfig,
                   std::chrono::milliseconds timeout,
                   uint32_t soft_ttl, uint32_t hard_ttl, uint32_t mcd_ttl)
        : m_pMemc(pMemc)
        , m_timeout(timeout)
        , m_pWorker(mxb::Worker::get_current())
        , m_soft_ttl(soft_ttl)
        , m_hard_ttl(hard_ttl)
        , m_mcd_ttl(mcd_ttl)
    {
    }

    bool connected() const
    {
        return m_connected;
    }

    void connect()
    {
        mxb_assert(!m_connected);
        mxb_assert(!m_connecting);

        m_connecting = true;

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis] () {
                // We check for an arbitrary key, doesn't matter which. In this context
                // it is a success if we are told it was not found.
                static const char key[] = "maxscale_memcachedstorage_ping";
                static const size_t key_length = sizeof(key) - 1;

                memcached_return_t rv = memcached_exist(sThis->m_pMemc, key, key_length);
                bool pinged = false;

                switch (rv)
                {
                case MEMCACHED_SUCCESS:
                case MEMCACHED_NOTFOUND:
                    pinged = true;
                    break;

                default:
                    MXB_ERROR("Could not ping memcached server, memcached caching will be "
                              "disabled: %s, %s",
                              memcached_strerror(sThis->m_pMemc, rv),
                              memcached_last_error_message(sThis->m_pMemc));
                }

                sThis->m_pWorker->execute([sThis, pinged]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            sThis->connection_checked(pinged);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            }, "memcached-cnct");
    }

    void reconnect()
    {
        if (!m_connecting)
        {
            m_reconnecting = true;

            auto now = std::chrono::steady_clock::now();

            if (now - m_connection_checked > m_timeout)
            {
                connect();
            }
        }
    }

    void connection_checked(bool success)
    {
        mxb_assert(m_connecting);

        m_connected = success;

        if (connected())
        {
            if (m_reconnecting)
            {
                // Reconnected after having been disconnected, let's log a note.
                MXB_NOTICE("Connected to Memcached storage. Caching is enabled.");
            }
        }

        m_connection_checked = std::chrono::steady_clock::now();
        m_connecting = false;
        m_reconnecting = false;
    }

    void connection_broken()
    {
        m_connected = false;
        m_connection_checked = std::chrono::steady_clock::now();
    }

private:
    memcached_st*                         m_pMemc;
    std::chrono::milliseconds             m_timeout;
    mxb::Worker*                          m_pWorker;
    uint32_t                              m_soft_ttl; // Soft TTL in milliseconds
    uint32_t                              m_hard_ttl; // Hard TTL in milliseconds
    uint32_t                              m_mcd_ttl;  // Hard TTL in seconds (rounded up if needed)
    bool                                  m_connected { false };
    std::chrono::steady_clock::time_point m_connection_checked;
    bool                                  m_connecting { false };
    bool                                  m_reconnecting { false };
};

}

MemcachedStorage::MemcachedStorage(const string& name,
                                   const Config& config,
                                   MemcachedConfig&& memcached_config)
    : m_name(name)
    , m_config(config)
    , m_mcd_ttl(config.hard_ttl)
    , m_memcached_config(std::move(memcached_config))
{
    // memcached supports a TTL with a granularity of a second.
    // A millisecond TTL is honored in get_value.
    if (m_mcd_ttl != 0)
    {
        m_mcd_ttl /= 1000;

        if (config.hard_ttl % 1000 != 0)
        {
            m_mcd_ttl += 1;
        }
    }
}

MemcachedStorage::~MemcachedStorage()
{
}

//static
const mxs::config::Specification& MemcachedStorage::specification()
{
    return MemcachedConfig::specification();
}

//static
bool MemcachedStorage::initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities)
{
    *pKind = CACHE_STORAGE_SHARED;
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT);
    return true;
}

//static
void MemcachedStorage::finalize()
{
}

//static
bool MemcachedStorage::get_limits(const mxs::ConfigParameters& parameters, Limits* pLimits)
{
    bool rv = true;

    int max_value_size = DEFAULT_MAX_VALUE_SIZE;

    string value = parameters.get_string(CN_MEMCACHED_MAX_VALUE_SIZE);

    if (!value.empty())
    {
        uint64_t size;
        if (get_suffixed_size(value, &size) && (size <= std::numeric_limits<uint32_t>::max()))
        {
            max_value_size = size;
        }
        else
        {
            MXB_ERROR("'%s' is not a valid value for '%s'.",
                      value.c_str(), CN_MEMCACHED_MAX_VALUE_SIZE);
            rv = false;
        }
    }

    if (rv)
    {
        *pLimits = Limits(max_value_size);
    }

    return rv;
}

//static
MemcachedStorage* MemcachedStorage::create(const string& name,
                                           const Config& config,
                                           const mxs::ConfigParameters& parameters)
{
    MemcachedStorage* pStorage = nullptr;

    if (config.invalidate != CACHE_INVALIDATE_NEVER)
    {
        MXB_ERROR("The storage storage_memcached does not support invalidation.");
    }
    else
    {
        if (config.max_size != 0)
        {
            MXB_WARNING("The storage storage_memcached does not support specifying "
                        "a maximum size of the cache storage.");
        }

        if (config.max_count != 0)
        {
            MXB_WARNING("The storage storage_memcached does not support specifying "
                        "a maximum number of items in the cache storage.");
        }

        MemcachedConfig memcached_config(name);
        if (MemcachedConfig::specification().validate(&memcached_config, parameters))
        {
            MXB_AT_DEBUG(bool success =) memcached_config.configure(parameters);
            mxb_assert(success);

            pStorage = new (std::nothrow) MemcachedStorage(name, config, std::move(memcached_config));
        }
    }

    return pStorage;
}

bool MemcachedStorage::create_token(std::shared_ptr<Storage::Token>* psToken)
{
    return MemcachedToken::create(&m_memcached_config,
                                  m_config.timeout,
                                  m_config.soft_ttl,
                                  m_config.hard_ttl,
                                  m_mcd_ttl,
                                  psToken);
}

void MemcachedStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

void MemcachedStorage::get_limits(Limits* pLimits)
{
    *pLimits = Limits(m_memcached_config.max_value_size);
}

cache_result_t MemcachedStorage::get_info(uint32_t what, json_t** ppInfo) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_value(Storage::Token* pToken,
                                           const CacheKey& key,
                                           uint32_t flags,
                                           uint32_t soft_ttl,
                                           uint32_t hard_ttl,
                                           GWBUF* pValue,
                                           const std::function<void (cache_result_t, GWBUF&&)>& cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->get_value(key, flags, soft_ttl, hard_ttl, pValue, cb);
}

cache_result_t MemcachedStorage::put_value(Token* pToken,
                                           const CacheKey& key,
                                           const std::vector<std::string>& invalidation_words,
                                           const GWBUF& value,
                                           const std::function<void (cache_result_t)>& cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->put_value(key, invalidation_words, value, cb);
}

cache_result_t MemcachedStorage::del_value(Token* pToken,
                                           const CacheKey& key,
                                           const std::function<void (cache_result_t)>& cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->del_value(key, cb);
}

cache_result_t MemcachedStorage::invalidate(Token* pToken,
                                            const std::vector<std::string>& words,
                                            const std::function<void (cache_result_t)>&)
{
    mxb_assert(!true);
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::clear(Token* pToken)
{
    mxb_assert(!true);
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_head(CacheKey* pKey, GWBUF* pHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_tail(CacheKey* pKey, GWBUF* pHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_ERROR;
}
