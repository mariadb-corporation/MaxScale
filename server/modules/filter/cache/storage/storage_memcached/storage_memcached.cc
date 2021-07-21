/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_memcached"
#include <maxscale/ccdefs.hh>
#include "../../cache_storage_api.hh"
#include "../storagemodule.hh"
#include "memcachedstorage.hh"

namespace
{

StorageModuleT<MemcachedStorage> module;

}

extern "C"
{

StorageModule* CacheGetStorageModule()
{
    return &module;
}
}
