/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_inmemory"
#include "inmemorystorage.hh"
#include <maxbase/alloc.h>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include "../../cache.hh"
#include "inmemorystoragest.hh"
#include "inmemorystoragemt.hh"

using std::unique_ptr;
using std::string;


namespace
{

const size_t INMEMORY_KEY_LENGTH = 2 * SHA512_DIGEST_LENGTH;

#if INMEMORY_KEY_LENGTH > CacheKey_MAXLEN
#error storage_inmemory key is too long.
#endif

struct
{
    Storage::Limits default_limits;
} this_unit =
{
    Storage::Limits(std::numeric_limits<uint32_t>::max()) // max_value_size
};

}

InMemoryStorage::InMemoryStorage(const string& name, const Config& config)
    : m_name(name)
    , m_config(config)
{
}

InMemoryStorage::~InMemoryStorage()
{
}

//static
bool InMemoryStorage::initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities)
{
    *pKind = CACHE_STORAGE_PRIVATE;
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT);

    return true;
}

//static
void InMemoryStorage::finalize()
{
}

//static
InMemoryStorage* InMemoryStorage::create(const char* zName,
                                         const Config& config,
                                         const std::string& arguments)
{
    mxb_assert(zName);

    if (config.max_count != 0)
    {
        MXS_WARNING("A maximum item count of %u specified, although 'storage_inmemory' "
                    "does not enforce such a limit.",
                    (unsigned int)config.max_count);
    }

    if (config.max_size != 0)
    {
        MXS_WARNING("A maximum size of %lu specified, although 'storage_inmemory' "
                    "does not enforce such a limit.",
                    (unsigned long)config.max_size);
    }

    if (!arguments.empty())
    {
        MXS_WARNING("Arguments '%s' provided, although 'storage_inmemory' does not "
                    "accept any arguments.", arguments.c_str());
    }

    unique_ptr<InMemoryStorage> sStorage;

    switch (config.thread_model)
    {
    case CACHE_THREAD_MODEL_ST:
        sStorage = InMemoryStorageST::create(zName, config);
        break;

    default:
        mxb_assert(!true);
        MXS_ERROR("Unknown thread model %d, creating multi-thread aware storage.",
                  (int)config.thread_model);

    case CACHE_THREAD_MODEL_MT:
        sStorage = InMemoryStorageMT::create(zName, config);
        break;
    }

    MXS_NOTICE("Storage module created.");

    return sStorage.release();
}

bool InMemoryStorage::create_token(std::shared_ptr<Storage::Token>* psToken)
{
    psToken->reset();
    return true;
}

void InMemoryStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

void InMemoryStorage::get_limits(Limits* pLimits)
{
    *pLimits = this_unit.default_limits;
}

cache_result_t InMemoryStorage::get_head(CacheKey* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t InMemoryStorage::get_tail(CacheKey* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t InMemoryStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t InMemoryStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t InMemoryStorage::do_get_info(uint32_t what, json_t** ppInfo) const
{
    *ppInfo = json_object();

    if (*ppInfo)
    {
        m_stats.fill(*ppInfo);
    }

    return *ppInfo ? CACHE_RESULT_OK : CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t InMemoryStorage::do_get_value(Token* pToken,
                                             const CacheKey& key,
                                             uint32_t flags,
                                             uint32_t soft_ttl,
                                             uint32_t hard_ttl,
                                             GWBUF** ppResult)
{
    mxb_assert(!pToken);

    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    Entries::const_iterator i = m_entries.find(key);

    if (i != m_entries.end())
    {
        m_stats.hits += 1;

        if (soft_ttl == CACHE_USE_CONFIG_TTL)
        {
            soft_ttl = m_config.soft_ttl;
        }

        if (hard_ttl == CACHE_USE_CONFIG_TTL)
        {
            hard_ttl = m_config.hard_ttl;
        }

        if (soft_ttl > hard_ttl)
        {
            soft_ttl = hard_ttl;
        }

        const Entry& entry = i->second;

        int64_t now = Cache::time_ms();

        bool is_hard_stale = hard_ttl == 0 ? false : (now - entry.time > hard_ttl);
        bool is_soft_stale = soft_ttl == 0 ? false : (now - entry.time > soft_ttl);
        bool include_stale = ((flags & CACHE_FLAGS_INCLUDE_STALE) != 0);

        if (is_hard_stale)
        {
            m_entries.erase(i);
            result |= CACHE_RESULT_DISCARDED;
        }
        else if (!is_soft_stale || include_stale)
        {
            size_t length = entry.value.size();

            *ppResult = gwbuf_alloc(length);

            if (*ppResult)
            {
                memcpy(GWBUF_DATA(*ppResult), entry.value.data(), length);

                result = CACHE_RESULT_OK;

                if (is_soft_stale)
                {
                    result |= CACHE_RESULT_STALE;
                }
            }
            else
            {
                result = CACHE_RESULT_OUT_OF_RESOURCES;
            }
        }
        else
        {
            mxb_assert(is_soft_stale);
            result |= CACHE_RESULT_STALE;
        }
    }
    else
    {
        m_stats.misses += 1;
    }

    return result;
}

cache_result_t InMemoryStorage::do_put_value(Token* pToken,
                                             const CacheKey& key,
                                             const std::vector<std::string>& invalidation_words,
                                             const GWBUF* pValue)
{
    mxb_assert(!pToken);
    mxb_assert(gwbuf_is_contiguous(pValue));

    if (!invalidation_words.empty())
    {
        MXB_ERROR("InMemoryStorage provided with invalidation words, "
                  "even though it does not support such.");
        mxb_assert(!true);
        return CACHE_RESULT_OUT_OF_RESOURCES;
    }

    size_t size = gwbuf_link_length(pValue);

    Entries::iterator i = m_entries.find(key);
    Entry* pEntry;

    if (i == m_entries.end())
    {
        m_stats.items += 1;

        pEntry = &m_entries[key];
        pEntry->value.resize(size);
    }
    else
    {
        m_stats.updates += 1;

        pEntry = &i->second;

        m_stats.size -= pEntry->value.size();

        if (size < pEntry->value.capacity())
        {
            // If the needed value is less than what is currently stored,
            // we shrink the buffer so as not to waste space.
            Value entry_value(size);
            pEntry->value.swap(entry_value);
        }
        else
        {
            pEntry->value.resize(size);
        }
    }

    m_stats.size += size;

    const uint8_t* pData = GWBUF_DATA(pValue);

    copy(pData, pData + size, pEntry->value.begin());
    pEntry->time = Cache::time_ms();

    return CACHE_RESULT_OK;
}

cache_result_t InMemoryStorage::do_del_value(Token* pToken, const CacheKey& key)
{
    mxb_assert(!pToken);
    Entries::iterator i = m_entries.find(key);

    if (i != m_entries.end())
    {
        mxb_assert(m_stats.size >= i->second.value.size());
        mxb_assert(m_stats.items > 0);

        m_stats.size -= i->second.value.size();
        m_stats.items -= 1;
        m_stats.deletes += 1;

        m_entries.erase(i);
    }

    return i != m_entries.end() ? CACHE_RESULT_OK : CACHE_RESULT_NOT_FOUND;
}

cache_result_t InMemoryStorage::do_invalidate(Token* pToken, const std::vector<std::string>& words)
{
    mxb_assert(!pToken);
    MXS_ERROR("InMemoryStorage cannot do invalidation.");
    mxb_assert(!true);
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t InMemoryStorage::do_clear(Token* pToken)
{
    mxb_assert(!pToken);
    m_stats.deletes += m_entries.size();
    m_stats.size = 0;
    m_stats.items = 0;

    m_entries.clear();

    return CACHE_RESULT_OK;
}

static void set_integer(json_t* pObject, const char* zName, size_t value)
{
    json_t* pValue = json_integer(value);

    if (pValue)
    {
        json_object_set_new(pObject, zName, pValue);
    }
}

void InMemoryStorage::Stats::fill(json_t* pObject) const
{
    set_integer(pObject, "size", size);
    set_integer(pObject, "items", items);
    set_integer(pObject, "hits", hits);
    set_integer(pObject, "misses", misses);
    set_integer(pObject, "updates", updates);
    set_integer(pObject, "deletes", deletes);
}
