/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_inmemory"
#include "inmemorystorage.hh"
#include <maxscale/alloc.h>
#include <maxscale/modutil.hh>
#include <maxscale/query_classifier.hh>
#include "inmemorystoragest.hh"
#include "inmemorystoragemt.hh"

using std::auto_ptr;
using std::string;


namespace
{

const size_t INMEMORY_KEY_LENGTH = 2 * SHA512_DIGEST_LENGTH;

#if INMEMORY_KEY_LENGTH > CACHE_KEY_MAXLEN
#error storage_inmemory key is too long.
#endif
}

InMemoryStorage::InMemoryStorage(const string& name, const CACHE_STORAGE_CONFIG& config)
    : m_name(name)
    , m_config(config)
{
}

InMemoryStorage::~InMemoryStorage()
{
}

bool InMemoryStorage::Initialize(uint32_t* pCapabilities)
{
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT);

    return true;
}

InMemoryStorage* InMemoryStorage::Create_instance(const char* zName,
                                                  const CACHE_STORAGE_CONFIG& config,
                                                  int argc,
                                                  char* argv[])
{
    mxb_assert(zName);

    if (config.max_count != 0)
    {
        MXS_WARNING("A maximum item count of %u specified, although 'storage_inMemory' "
                    "does not enforce such a limit.",
                    (unsigned int)config.max_count);
    }

    if (config.max_size != 0)
    {
        MXS_WARNING("A maximum size of %lu specified, although 'storage_inMemory' "
                    "does not enforce such a limit.",
                    (unsigned long)config.max_size);
    }

    auto_ptr<InMemoryStorage> sStorage;

    switch (config.thread_model)
    {
    case CACHE_THREAD_MODEL_ST:
        sStorage = InMemoryStorageST::Create(zName, config, argc, argv);
        break;

    default:
        mxb_assert(!true);
        MXS_ERROR("Unknown thread model %d, creating multi-thread aware storage.",
                  (int)config.thread_model);

    case CACHE_THREAD_MODEL_MT:
        sStorage = InMemoryStorageMT::Create(zName, config, argc, argv);
        break;
    }

    MXS_NOTICE("Storage module created.");

    return sStorage.release();
}

void InMemoryStorage::get_config(CACHE_STORAGE_CONFIG* pConfig)
{
    *pConfig = m_config;
}

cache_result_t InMemoryStorage::get_head(CACHE_KEY* pKey, GWBUF** ppHead) const
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t InMemoryStorage::get_tail(CACHE_KEY* pKey, GWBUF** ppHead) const
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

cache_result_t InMemoryStorage::do_get_value(const CACHE_KEY& key,
                                             uint32_t flags,
                                             uint32_t soft_ttl,
                                             uint32_t hard_ttl,
                                             GWBUF**  ppResult)
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    Entries::iterator i = m_entries.find(key);

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

        Entry& entry = i->second;

        uint32_t now = time(NULL);

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

cache_result_t InMemoryStorage::do_put_value(const CACHE_KEY& key, const GWBUF& value)
{
    mxb_assert(GWBUF_IS_CONTIGUOUS(&value));

    size_t size = GWBUF_LENGTH(&value);

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

    const uint8_t* pData = GWBUF_DATA(&value);

    copy(pData, pData + size, pEntry->value.begin());
    pEntry->time = time(NULL);

    return CACHE_RESULT_OK;
}

cache_result_t InMemoryStorage::do_del_value(const CACHE_KEY& key)
{
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

static void set_integer(json_t* pObject, const char* zName, size_t value)
{
    json_t* pValue = json_integer(value);

    if (pValue)
    {
        json_object_set(pObject, zName, pValue);
        json_decref(pValue);
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
