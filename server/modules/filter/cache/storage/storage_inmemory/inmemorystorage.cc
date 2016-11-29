/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_inmemory"
#include "inmemorystorage.h"
#include <openssl/sha.h>
#include <algorithm>
#include <set>
#include <maxscale/alloc.h>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>

using std::set;
using std::string;


namespace
{

const size_t INMEMORY_KEY_LENGTH = 2 * SHA512_DIGEST_LENGTH;

#if INMEMORY_KEY_LENGTH > CACHE_KEY_MAXLEN
#error storage_inmemory key is too long.
#endif

}

InMemoryStorage::InMemoryStorage(const string& name,
                                 uint32_t ttl)
    : name_(name)
    , ttl_(ttl)
{
}

InMemoryStorage::~InMemoryStorage()
{
}

cache_result_t InMemoryStorage::get_key(const char* zdefault_db, const GWBUF* pquery, CACHE_KEY* pkey)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(pquery));

    int n;
    bool fullnames = true;
    char** pztables = qc_get_table_names(const_cast<GWBUF*>(pquery), &n, fullnames);

    set<string> dbs; // Elements in set are sorted.

    for (int i = 0; i < n; ++i)
    {
        char *ztable = pztables[i];
        char *zdot = strchr(ztable, '.');

        if (zdot)
        {
            *zdot = 0;
            dbs.insert(ztable);
        }
        else if (zdefault_db)
        {
            // If zdefault_db is NULL, then there will be a table for which we
            // do not know the database. However, that will fail in the server,
            // so nothing will be stored.
            dbs.insert(zdefault_db);
        }
        MXS_FREE(ztable);
    }
    MXS_FREE(pztables);

    // dbs now contain each accessed database in sorted order. Now copy them to a single string.
    string tag;
    for (set<string>::const_iterator i = dbs.begin(); i != dbs.end(); ++i)
    {
        tag.append(*i);
    }

    memset(pkey->data, 0, CACHE_KEY_MAXLEN);

    const unsigned char* pdata;

    // We store the databases in the first half of the key. That will ensure that
    // identical queries targeting different default databases will not clash.
    // This will also mean that entries related to the same databases will
    // be placed near each other.
    pdata = reinterpret_cast<const unsigned char*>(tag.data());
    SHA512(pdata, tag.length(), reinterpret_cast<unsigned char*>(pkey->data));

    char *psql;
    int length;

    modutil_extract_SQL(const_cast<GWBUF*>(pquery), &psql, &length);

    // Then we store the query itself in the second half of the key.
    pdata = reinterpret_cast<const unsigned char*>(psql);
    SHA512(pdata, length, reinterpret_cast<unsigned char*>(pkey->data) + SHA512_DIGEST_LENGTH);

    return CACHE_RESULT_OK;
}

cache_result_t InMemoryStorage::do_get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppresult)
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    Entries::iterator i = entries_.find(key);

    if (i != entries_.end())
    {
        Entry& entry = i->second;

        uint32_t now = time(NULL);

        bool is_stale = (now - entry.time > ttl_);

        if (!is_stale || ((flags & CACHE_FLAGS_INCLUDE_STALE) != 0))
        {
            size_t length = entry.value.size();

            *ppresult = gwbuf_alloc(length);

            if (*ppresult)
            {
                memcpy(GWBUF_DATA(*ppresult), entry.value.data(), length);

                if (is_stale)
                {
                    result = CACHE_RESULT_STALE;
                }
                else
                {
                    result = CACHE_RESULT_OK;
                }
            }
            else
            {
                result = CACHE_RESULT_OUT_OF_RESOURCES;
            }
        }
        else
        {
            MXS_NOTICE("Cache item is stale, not using.");
        }
    }

    return result;
}

cache_result_t InMemoryStorage::do_put_value(const CACHE_KEY& key, const GWBUF* pvalue)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(pvalue));

    size_t size = GWBUF_LENGTH(pvalue);

    Entry& entry = entries_[key];

    if (size < entry.value.capacity())
    {
        // If the needed value is less than what is currently stored,
        // we shrink the buffer so as not to waste space.
        Value value(size);
        entry.value.swap(value);
    }
    else
    {
        entry.value.resize(size);
    }

    const uint8_t* pdata = GWBUF_DATA(pvalue);

    copy(pdata, pdata + size, entry.value.begin());
    entry.time = time(NULL);

    return CACHE_RESULT_OK;
}

cache_result_t InMemoryStorage::do_del_value(const CACHE_KEY& key)
{
    Entries::iterator i = entries_.find(key);

    if (i != entries_.end())
    {
        entries_.erase(i);
    }

    return i != entries_.end() ? CACHE_RESULT_OK : CACHE_RESULT_NOT_FOUND;
}
