/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cache.hh"
#include <new>
#include <set>
#include <string>
#include <zlib.h>
#include <maxscale/alloc.h>
#include <maxscale/buffer.h>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>
#include <maxscale/paths.h>
#include "storagefactory.hh"
#include "storage.hh"

using namespace std;

Cache::Cache(const std::string&  name,
             const CACHE_CONFIG* pConfig,
             SCacheRules         sRules,
             SStorageFactory     sFactory)
    : m_name(name)
    , m_config(*pConfig)
    , m_sRules(sRules)
    , m_sFactory(sFactory)
{
}

Cache::~Cache()
{
}

//static
bool Cache::Create(const CACHE_CONFIG& config,
                   CacheRules**        ppRules,
                   StorageFactory**    ppFactory)
{
    CacheRules* pRules = NULL;
    StorageFactory* pFactory = NULL;

    if (config.rules)
    {
        pRules = CacheRules::load(config.rules, config.debug);
    }
    else
    {
        pRules = CacheRules::create(config.debug);
    }

    if (pRules)
    {
        pFactory = StorageFactory::Open(config.storage);

        if (pFactory)
        {
            *ppFactory = pFactory;
            *ppRules = pRules;
        }
        else
        {
            MXS_ERROR("Could not open storage factory '%s'.", config.storage);
            delete pRules;
        }
    }
    else
    {
        MXS_ERROR("Could not create rules.");
    }

    return pFactory != NULL;
}

void Cache::show(DCB* pDcb) const
{
    bool showed = false;
    json_t* pInfo = get_info(INFO_ALL);

    if (pInfo)
    {
        size_t flags = JSON_PRESERVE_ORDER;
        size_t indent = 2;
        char* z = json_dumps(pInfo, JSON_PRESERVE_ORDER | JSON_INDENT(indent));

        if (z)
        {
            dcb_printf(pDcb, "%s\n", z);
            free(z);
            showed = true;
        }

        json_decref(pInfo);
    }

    if (!showed)
    {
        // So as not to upset anyone expecting a JSON object.
        dcb_printf(pDcb, "{\n}\n");
    }
}

cache_result_t Cache::get_key(const char* zDefault_db,
                              const GWBUF* pQuery,
                              CACHE_KEY* pKey) const
{
    // TODO: Take config into account.
    return get_default_key(zDefault_db, pQuery, pKey);
}

//static
cache_result_t Cache::get_default_key(const char* zDefault_db,
                                      const GWBUF* pQuery,
                                      CACHE_KEY* pKey)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(pQuery));

    int n;
    bool fullnames = true;
    char** pzTables = qc_get_table_names(const_cast<GWBUF*>(pQuery), &n, fullnames);

    set<string> dbs; // Elements in set are sorted.

    for (int i = 0; i < n; ++i)
    {
        char *zTable = pzTables[i];
        char *zDot = strchr(zTable, '.');

        if (zDot)
        {
            *zDot = 0;
            dbs.insert(zTable);
        }
        else if (zDefault_db)
        {
            // If zdefault_db is NULL, then there will be a table for which we
            // do not know the database. However, that will fail in the server,
            // so nothing will be stored.
            dbs.insert(zDefault_db);
        }
        MXS_FREE(zTable);
    }
    MXS_FREE(pzTables);

    // dbs now contain each accessed database in sorted order. Now copy them to a single string.
    string tag;
    for (set<string>::const_iterator i = dbs.begin(); i != dbs.end(); ++i)
    {
        tag.append(*i);
    }

    const unsigned char* pData;

    // We hash the databases in the first half of the key. That will ensure that
    // identical queries targeting different default databases will not clash.
    pData = reinterpret_cast<const unsigned char*>(tag.data());
    uint64_t table_crc = crc32(0, Z_NULL, 0);
    table_crc = crc32(table_crc, pData, tag.length());

    char *pSql;
    int length;

    modutil_extract_SQL(const_cast<GWBUF*>(pQuery), &pSql, &length);

    // Then we hash the query itself in the second half of the key.
    pData = reinterpret_cast<const unsigned char*>(pSql);
    uint64_t stmt_crc = crc32(0, Z_NULL, 0);
    stmt_crc = crc32(stmt_crc, pData, length);

    pKey->data = (table_crc << 32) | stmt_crc;

    return CACHE_RESULT_OK;
}

bool Cache::should_store(const char* zDefaultDb, const GWBUF* pQuery)
{
    return m_sRules->should_store(zDefaultDb, pQuery);
}

bool Cache::should_use(const MXS_SESSION* pSession)
{
    return m_sRules->should_use(pSession);
}

json_t* Cache::do_get_info(uint32_t what) const
{
    json_t* pInfo = json_object();

    if (pInfo)
    {
        if (what & INFO_RULES)
        {
            json_t* pRules = const_cast<json_t*>(m_sRules->json());

            json_object_set(pInfo, "rules", pRules); // Increases ref-count of pRules, we ignore failure.
        }
    }

    return pInfo;
}
