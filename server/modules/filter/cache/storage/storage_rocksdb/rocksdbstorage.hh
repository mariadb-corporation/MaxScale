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

#include <maxscale/cppdefs.hh>
#include <memory>
#include <string>
#include <rocksdb/utilities/db_ttl.h>
#include "../../cache_storage_api.h"

class RocksDBStorage
{
public:
    typedef std::unique_ptr<RocksDBStorage> SRocksDBStorage;

    static bool Initialize();

    static SRocksDBStorage Create(const char* zName, uint32_t ttl, int argc, char* argv[]);
    ~RocksDBStorage();

    cache_result_t getInfo(uint32_t flags, json_t** ppInfo) const;
    cache_result_t getKey(const char* zDefaultDB, const GWBUF* pQuery, CACHE_KEY* pKey);
    cache_result_t getValue(const CACHE_KEY* pKey, uint32_t flags, GWBUF** ppResult);
    cache_result_t putValue(const CACHE_KEY* pKey, const GWBUF* pValue);
    cache_result_t delValue(const CACHE_KEY* pKey);

private:
    RocksDBStorage(std::unique_ptr<rocksdb::DBWithTTL>& sDb,
                   const std::string& name,
                   const std::string& path,
                   uint32_t ttl);

    RocksDBStorage(const RocksDBStorage&) = delete;
    RocksDBStorage& operator = (const RocksDBStorage&) = delete;

    static SRocksDBStorage Create(const std::string& storageDirectory,
                                  const char* zName,
                                  uint32_t ttl,
                                  bool collectStatistics);

    static const rocksdb::WriteOptions& writeOptions()
    {
        return s_writeOptions;
    }

private:
    std::unique_ptr<rocksdb::DBWithTTL> m_sDb;
    std::string                         m_name;
    std::string                         m_path;
    uint32_t                            m_ttl;

    static rocksdb::WriteOptions        s_writeOptions;
};
