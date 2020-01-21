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

#define MXS_MODULE_NAME "storage_memcached"
#include "memcachedstorage.hh"
#include <memory>
#include <libmemcached/memcached.h>
#include <libmemcached-1.0/strerror.h>
#include <maxbase/worker.hh>
#include <maxscale/config_common.hh>
#include <maxscale/threadpool.hh>

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

    static bool create(const string& memcached_config, time_t ttl, shared_ptr<Storage::Token>* psToken)
    {
        bool rv = false;
        memcached_st* pMemc = memcached(memcached_config.c_str(), memcached_config.size());

        if (pMemc)
        {
            memcached_return_t mrv = memcached_behavior_set(pMemc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);

            if (memcached_success(mrv))
            {
                MemcachedToken* pToken = new (std::nothrow) MemcachedToken(pMemc, ttl);

                if (pToken)
                {
                    psToken->reset(pToken);
                    rv = true;
                }
                else
                {
                    memcached_free(pMemc);
                }
            }
            else
            {
                MXS_ERROR("Could not turn on memcached binary protocol: %s",
                          memcached_strerror(pMemc, mrv));
                memcached_free(pMemc);
            }
        }
        else
        {
            MXS_ERROR("Could not create memcached handle, are the arguments '%s' valid?",
                      memcached_config.c_str());
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
                size_t nData;
                uint32_t flags;
                memcached_return_t mrv;

                char* pData = memcached_get(sThis->m_pMemc, mkey.data(), mkey.size(), &nData, &flags, &mrv);

                GWBUF* pValue = nullptr;
                cache_result_t rv;

                if (memcached_success(mrv))
                {
                    if (pData)
                    {
                        pValue = gwbuf_alloc_and_load(nData, pData);
                        MXS_FREE(pData);
                        rv = CACHE_RESULT_OK;
                    }
                    else
                    {
                        // TODO: With the textual protocol you could get this; NULL returned but
                        // TODO: no error reported. Does not seem to be a problem with the binary
                        // TODO: protocol enabled.
                        MXS_WARNING("NULL value returned from memcached, but no error reported.");
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
                        MXS_WARNING("Failed when fetching cached value from memcached: %s, %s",
                                    memcached_strerror(sThis->m_pMemc, mrv),
                                    memcached_last_error_message(sThis->m_pMemc));
                        rv = CACHE_RESULT_ERROR;
                    }
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
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             std::function<void (cache_result_t)> cb)
    {
        vector<char> mkey = key.to_vector();

        GWBUF* pClone = gwbuf_clone(const_cast<GWBUF*>(pValue));
        MXS_ABORT_IF_NULL(pClone);

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, mkey, pClone, cb]() {
                const uint32_t flags = 0;
                memcached_return_t mrv = memcached_set(sThis->m_pMemc, mkey.data(), mkey.size(),
                                                       reinterpret_cast<const char*>(GWBUF_DATA(pClone)),
                                                       GWBUF_LENGTH(pClone), sThis->m_ttl, flags);
                cache_result_t rv;

                if (memcached_success(mrv))
                {
                    rv = CACHE_RESULT_OK;
                }
                else
                {
                    MXS_WARNING("Failed when storing cache value to memcached: %s, %s",
                                memcached_strerror(sThis->m_pMemc, mrv),
                                memcached_last_error_message(sThis->m_pMemc));
                    rv = CACHE_RESULT_ERROR;
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
                memcached_return_t mrv = memcached_delete(sThis->m_pMemc, mkey.data(), mkey.size(), 0);

                cache_result_t rv;

                if (memcached_success(mrv))
                {
                    rv = CACHE_RESULT_OK;
                }
                else
                {
                    MXS_WARNING("Failed when deleting cached value from memcached: %s, %s",
                                memcached_strerror(sThis->m_pMemc, mrv),
                                memcached_last_error_message(sThis->m_pMemc));
                    rv = CACHE_RESULT_ERROR;
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
    MemcachedToken(memcached_st* pMemc, time_t ttl)
        : m_pMemc(pMemc)
        , m_pWorker(mxb::Worker::get_current())
        , m_ttl(ttl)
    {
    }

private:
    memcached_st* m_pMemc;
    mxb::Worker*  m_pWorker;
    time_t        m_ttl;
};

}

MemcachedStorage::MemcachedStorage(const string& name,
                                   const Config& config,
                                   uint32_t max_value_size,
                                   const string& memcached_config)
    : m_name(name)
    , m_config(config)
    , m_limits(max_value_size)
    , m_memcached_config(memcached_config)
{
    if (config.soft_ttl != config.hard_ttl)
    {
        MXS_WARNING("The storage storage_memcached does not distinguish between "
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
        m_ttl = config.hard_ttl / 1000;

        if (m_ttl == 0)
        {
            MXS_WARNING("Memcached does not support a ttl (%u ms) less than 1 second. "
                        "A ttl of 1 second is used.", config.hard_ttl);
            m_ttl = 1;
        }
    }
}

MemcachedStorage::~MemcachedStorage()
{
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
MemcachedStorage* MemcachedStorage::create(const std::string& name,
                                           const Config& config,
                                           const std::string& argument_string)
{
    MemcachedStorage* pStorage = nullptr;

    if (config.invalidate != CACHE_INVALIDATE_NEVER)
    {
        MXS_ERROR("The storage storage_memcached does not support invalidation.");
    }
    else
    {
        if (config.max_size != 0)
        {
            MXS_WARNING("The storage storage_memcached does not support specifying "
                        "a maximum size of the cache storage.");
        }

        if (config.max_count != 0)
        {
            MXS_WARNING("The storage storage_memcached does not support specifying "
                        "a maximum number of items in the cache storage.");
        }

        map<string, string> arguments;

        if (Storage::split_arguments(argument_string, &arguments))
        {
            bool error = false;

            mxb::Host host;
            int max_value_size = DEFAULT_MAX_VALUE_SIZE;

            decltype(arguments)::iterator it;

            it = arguments.find(CN_STORAGE_ARG_SERVER);

            if (it != arguments.end())
            {
                if (!Storage::get_host(it->second, DEFAULT_MEMCACHED_PORT, &host))
                {
                    error = true;
                }

                arguments.erase(it);
            }
            else
            {
                MXS_ERROR("The mandatory argument '%s' is missing.", CN_STORAGE_ARG_SERVER);
                error = true;
            }

            it = arguments.find(CN_MEMCACHED_MAX_VALUE_SIZE);

            if (it != arguments.end())
            {
                uint64_t size;
                if (get_suffixed_size(it->second, &size) && (size <= std::numeric_limits<uint32_t>::max()))
                {
                    max_value_size = size;
                }
                else
                {
                    MXS_ERROR("'%s' is not a valid value for '%s'.",
                              it->second.c_str(), CN_MEMCACHED_MAX_VALUE_SIZE);
                    error = true;
                }

                arguments.erase(it);
            }

            for (const auto& kv : arguments)
            {
                MXS_WARNING("Unknown `storage_memcached` argument: %s=%s",
                            kv.first.c_str(), kv.second.c_str());
            }

            if (!error)
            {
                MXS_NOTICE("Resultsets up to %u bytes in size will be cached by '%s'.",
                           max_value_size, name.c_str());

                std::string memcached_arguments("--SERVER=");
                memcached_arguments += host.address();
                memcached_arguments += ":";
                memcached_arguments += std::to_string(host.port());

                pStorage = new (std::nothrow) MemcachedStorage(name,
                                                               config,
                                                               max_value_size,
                                                               memcached_arguments);
            }
        }
        else
        {
            MXS_ERROR("Could not create memcached handle.");
        }
    }

    return pStorage;
}

bool MemcachedStorage::create_token(std::shared_ptr<Storage::Token>* psToken)
{
    return MemcachedToken::create(m_memcached_config, m_ttl, psToken);
}

void MemcachedStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

void MemcachedStorage::get_limits(Limits* pLimits)
{
    *pLimits = m_limits;
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
                                           GWBUF** ppValue,
                                           std::function<void (cache_result_t, GWBUF*)> cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->get_value(key, flags, soft_ttl, hard_ttl, ppValue, cb);
}

cache_result_t MemcachedStorage::put_value(Token* pToken,
                                           const CacheKey& key,
                                           const std::vector<std::string>& invalidation_words,
                                           const GWBUF* pValue,
                                           std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->put_value(key, invalidation_words, pValue, cb);
}

cache_result_t MemcachedStorage::del_value(Token* pToken,
                                           const CacheKey& key,
                                           std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->del_value(key, cb);
}

cache_result_t MemcachedStorage::invalidate(Token* pToken,
                                            const std::vector<std::string>& words,
                                            std::function<void (cache_result_t)>)
{
    mxb_assert(!true);
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::clear(Token* pToken)
{
    mxb_assert(!true);
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_head(CacheKey* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_tail(CacheKey* pKey, GWBUF** ppHead)
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
