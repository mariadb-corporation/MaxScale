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

#define MXS_MODULE_NAME "storage_rocksdb"
#include "rocksdbinternals.hh"
#include <rocksdb/env.h>
#include <util/coding.h>
#include <maxscale/debug.h>

/**
 * The following is basically a copy from RocksDB/utilities/ttl/db_ttl_impl.cc:160
 * but note that the here we claim the data is stale if we fail to get the time
 * while the original code claims it is fresh.
 */
bool RocksDBInternals::is_stale(const rocksdb::Slice& value, int32_t ttl, rocksdb::Env* pEnv)
{
    if (ttl <= 0)
    {
        // Data is fresh if TTL is non-positive
        return false;
    }

    int64_t curtime;
    if (!pEnv->GetCurrentTime(&curtime).ok())
    {
        return true;  // Treat the data as stale if could not get current time
    }

    int32_t timestamp = rocksdb::DecodeFixed32(value.data() + value.size() - TS_LENGTH);
    return (timestamp + ttl) < curtime;
}

int32_t RocksDBInternals::extract_timestamp(const rocksdb::Slice& value)
{
    ss_dassert(value.size() >= TS_LENGTH);
    return rocksdb::DecodeFixed32(value.data() + value.size() - TS_LENGTH);
}
