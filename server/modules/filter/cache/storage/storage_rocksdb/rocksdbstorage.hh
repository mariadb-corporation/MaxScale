#pragma once
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

#include <maxscale/cppdefs.hh>
#include <memory>
#include <string>
#include <rocksdb/utilities/db_ttl.h>
#include "../../cache_storage_api.h"

class RocksDBStorage
{
public:
    typedef std::unique_ptr<RocksDBStorage> SRocksDBStorage;

    static bool Initialize(uint32_t* pCapabilities);

    static RocksDBStorage* Create_instance(const char* zName,
                                           const CACHE_STORAGE_CONFIG& config,
                                           int argc, char* argv[]);
    ~RocksDBStorage();

    void get_config(CACHE_STORAGE_CONFIG* pConfig);
    cache_result_t get_info(uint32_t flags, json_t** ppInfo) const;
    cache_result_t get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppResult);
    cache_result_t put_value(const CACHE_KEY& key, const GWBUF& value);
    cache_result_t del_value(const CACHE_KEY& key);

    cache_result_t get_head(CACHE_KEY* pKey, GWBUF** ppHead) const;
    cache_result_t get_tail(CACHE_KEY* pKey, GWBUF** ppHead) const;
    cache_result_t get_size(uint64_t* pSize) const;
    cache_result_t get_items(uint64_t* pItems) const;

private:
    RocksDBStorage(const std::string& name,
                   const CACHE_STORAGE_CONFIG& config,
                   const std::string& path,
                   std::unique_ptr<rocksdb::DBWithTTL>& sDb);

    RocksDBStorage(const RocksDBStorage&) = delete;
    RocksDBStorage& operator = (const RocksDBStorage&) = delete;

    static RocksDBStorage* Create(const char* zName,
                                  const CACHE_STORAGE_CONFIG& config,
                                  const std::string& storage_directory,
                                  bool collect_statistics);

    static const rocksdb::WriteOptions& Write_options()
    {
        return s_write_options;
    }

private:
    std::string                         m_name;
    const CACHE_STORAGE_CONFIG          m_config;
    std::string                         m_path;
    std::unique_ptr<rocksdb::DBWithTTL> m_sDb;

    static rocksdb::WriteOptions        s_write_options;
};
