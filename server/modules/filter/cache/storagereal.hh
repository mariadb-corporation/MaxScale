#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include "storage.hh"

class StorageReal : public Storage
{
public:
    ~StorageReal();

    void get_config(CACHE_STORAGE_CONFIG* pConfig);

    cache_result_t get_info(uint32_t flags,
                            json_t** ppInfo) const;

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue) const;

    cache_result_t put_value(const CACHE_KEY& key,
                             const GWBUF* pValue);

    cache_result_t del_value(const CACHE_KEY& key);

    cache_result_t get_head(CACHE_KEY* pKey,
                            GWBUF** ppValue) const;

    cache_result_t get_tail(CACHE_KEY* pKey,
                            GWBUF** ppValue) const;

    cache_result_t get_size(uint64_t* pSize) const;

    cache_result_t get_items(uint64_t* pItems) const;

private:
    friend class StorageFactory;

    StorageReal(CACHE_STORAGE_API* pApi, CACHE_STORAGE* pStorage);

    StorageReal(const StorageReal&);
    StorageReal& operator = (const StorageReal&);

private:
    CACHE_STORAGE_API* m_pApi;
    CACHE_STORAGE*     m_pStorage;
};
