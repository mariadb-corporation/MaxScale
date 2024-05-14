/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "storage_redis.hh"
#include "../storagemodule.hh"
#include "redisstorage.hh"

// redisInitOpenSSL() need not be called, as OpenSSL is always initialized in gateway.cc

namespace
{

StorageModuleT<RedisStorage> module;

}

extern "C"
{

StorageModule* CacheGetStorageModule()
{
    return &module;
}
}
