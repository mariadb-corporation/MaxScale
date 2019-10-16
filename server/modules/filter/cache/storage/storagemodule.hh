/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

template<class StorageType>
class StorageModule
{
public:
    static bool initialize(uint32_t* pCapabilities)
    {
        return StorageType::Initialize(pCapabilities);
    }

    static Storage* createInstance(const char* zName,
                                   const CACHE_STORAGE_CONFIG* pConfig,
                                   int argc,
                                   char* argv[])
    {
        mxb_assert(zName);
        mxb_assert(pConfig);

        StorageType* pStorage = NULL;

        MXS_EXCEPTION_GUARD(pStorage = StorageType::Create_instance(zName, *pConfig, argc, argv));

        return pStorage;
    }

    static void freeInstance(Storage* pInstance)
    {
        MXS_EXCEPTION_GUARD(delete reinterpret_cast<StorageType*>(pInstance));
    }

    static CACHE_STORAGE_API s_api;
};

template<class StorageType>
CACHE_STORAGE_API StorageModule<StorageType>::s_api =
{
    &StorageModule<StorageType>::initialize,
    &StorageModule<StorageType>::createInstance,
    &StorageModule<StorageType>::freeInstance,
};
