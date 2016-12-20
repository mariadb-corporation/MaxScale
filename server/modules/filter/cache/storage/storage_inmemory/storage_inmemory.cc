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
#include <maxscale/cppdefs.hh>
#include <inttypes.h>
#include "../../cache_storage_api.h"
#include "inmemorystoragest.hh"
#include "inmemorystoragemt.hh"

using std::auto_ptr;

namespace
{

bool initialize(uint32_t* pcapabilities)
{
    *pcapabilities = CACHE_STORAGE_CAP_ST;
    *pcapabilities = CACHE_STORAGE_CAP_MT;

    return true;
}

CACHE_STORAGE* createInstance(const char* zname,
                              const CACHE_STORAGE_CONFIG* pConfig,
                              int argc, char* argv[])
{
    ss_dassert(zname);

    if (pConfig->max_count != 0)
    {
        MXS_WARNING("A maximum item count of %u specified, although 'storage_inMemory' "
                    "does not enforce such a limit.", (unsigned int)pConfig->max_count);
    }

    if (pConfig->max_size != 0)
    {
        MXS_WARNING("A maximum size of %lu specified, although 'storage_inMemory' "
                    "does not enforce such a limit.", (unsigned long)pConfig->max_size);
    }

    auto_ptr<InMemoryStorage> sStorage;

    switch (pConfig->thread_model)
    {
    case CACHE_THREAD_MODEL_ST:
        MXS_EXCEPTION_GUARD(sStorage = InMemoryStorageST::create(zname, *pConfig, argc, argv));
        break;

    default:
        ss_dassert(!true);
        MXS_ERROR("Unknown thread model %d, creating multi-thread aware storage.",
                  (int)pConfig->thread_model);
    case CACHE_THREAD_MODEL_MT:
        MXS_EXCEPTION_GUARD(sStorage = InMemoryStorageMT::create(zname, *pConfig, argc, argv));
        break;
    }

    if (sStorage.get())
    {
        MXS_NOTICE("Storage module created.");
    }

    return reinterpret_cast<CACHE_STORAGE*>(sStorage.release());
}

cache_result_t getKey(const char* zdefault_db,
                      const GWBUF* pquery,
                      CACHE_KEY* pkey)
{
    // zdefault_db may be NULL.
    ss_dassert(pquery);
    ss_dassert(pkey);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = InMemoryStorage::get_key(zdefault_db, pquery, pkey));

    return result;
}

void freeInstance(CACHE_STORAGE* pinstance)
{
    MXS_EXCEPTION_GUARD(delete reinterpret_cast<InMemoryStorage*>(pinstance));
}

void getConfig(CACHE_STORAGE* pStorage,
               CACHE_STORAGE_CONFIG* pConfig)
{
    ss_dassert(pStorage);
    ss_dassert(pConfig);

    MXS_EXCEPTION_GUARD(reinterpret_cast<InMemoryStorage*>(pStorage)->get_config(pConfig));
}

cache_result_t getInfo(CACHE_STORAGE* pStorage,
                       uint32_t       what,
                       json_t**       ppInfo)
{
    ss_dassert(pStorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pStorage)->get_info(what, ppInfo));

    return result;
}

cache_result_t getValue(CACHE_STORAGE* pstorage,
                        const CACHE_KEY* pkey,
                        uint32_t flags,
                        GWBUF** ppresult)
{
    ss_dassert(pstorage);
    ss_dassert(pkey);
    ss_dassert(ppresult);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pstorage)->get_value(*pkey,
                                                                                         flags,
                                                                                         ppresult));

    return result;
}

cache_result_t putValue(CACHE_STORAGE* pstorage,
                        const CACHE_KEY* pkey,
                        const GWBUF* pvalue)
{
    ss_dassert(pstorage);
    ss_dassert(pkey);
    ss_dassert(pvalue);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pstorage)->put_value(*pkey, pvalue));

    return result;
}

cache_result_t delValue(CACHE_STORAGE* pstorage,
                        const CACHE_KEY* pkey)
{
    ss_dassert(pstorage);
    ss_dassert(pkey);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pstorage)->del_value(*pkey));

    return result;
}

cache_result_t getHead(CACHE_STORAGE* pstorage,
                       CACHE_KEY* pkey,
                       GWBUF** pphead)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pstorage)->get_head(pkey, pphead));

    return result;
}

cache_result_t getTail(CACHE_STORAGE* pstorage,
                       CACHE_KEY* pkey,
                       GWBUF** pptail)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pstorage)->get_tail(pkey, pptail));

    return result;
}

cache_result_t getSize(CACHE_STORAGE* pstorage,
                       uint64_t* psize)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pstorage)->get_size(psize));

    return result;
}


cache_result_t getItems(CACHE_STORAGE* pstorage,
                        uint64_t* pitems)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<InMemoryStorage*>(pstorage)->get_items(pitems));

    return result;
}

}

extern "C"
{

CACHE_STORAGE_API* CacheGetStorageAPI()
{
    static CACHE_STORAGE_API api =
        {
            initialize,
            createInstance,
            getKey,
            freeInstance,
            getConfig,
            getInfo,
            getValue,
            putValue,
            delValue,
            getHead,
            getTail,
            getSize,
            getItems
        };

    return &api;
}

}
