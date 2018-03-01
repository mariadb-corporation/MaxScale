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

    char *pSql;
    int length;

    modutil_extract_SQL(const_cast<GWBUF*>(pQuery), &pSql, &length);

    uint64_t crc1 = crc32(0, Z_NULL, 0);

    const Bytef* pData;

    if (zDefault_db)
    {
        pData = reinterpret_cast<const Bytef*>(zDefault_db);
        crc1 = crc32(crc1, pData, strlen(zDefault_db));
    }

    pData = reinterpret_cast<const Bytef*>(pSql);

    crc1 = crc32(crc1, pData, length);
    uint64_t crc2 = crc32(crc1, pData, length);

    pKey->data = (crc1 << 32 | crc2);

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
