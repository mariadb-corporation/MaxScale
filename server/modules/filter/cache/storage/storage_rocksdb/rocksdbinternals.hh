#pragma once
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

#include <maxscale/cdefs.h>
#include <rocksdb/env.h>
#include <rocksdb/version.h>
#include <rocksdb/slice.h>

#if (ROCKSDB_MAJOR != 4) || (ROCKSDB_MINOR != 9)
#error RocksDBStorage was created with knowledge of RocksDB 4.9 internals.\
       The version used is something else. Ensure the knowledge is still applicable.
#endif

namespace RocksDBInternals
{

/**
 * The length of the timestamp when stashed after the actual value.
 *
 * See RocksDB/utilities/ttl/db_ttl_impl.h
 */
static const uint32_t TS_LENGTH = sizeof(int32_t);

bool IsStale(const rocksdb::Slice& slice, int32_t ttl, rocksdb::Env* pEnv);

}
