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
#include "storage.h"

class StorageReal : public Storage
{
public:
    ~StorageReal();

    cache_result_t get_info(uint32_t flags,
                                    json_t** ppInfo) const;

    cache_result_t get_key(const char* zDefaultDb,
                           const GWBUF* pQuery,
                           CACHE_KEY* pKey);

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags,
                             GWBUF** ppValue);

    cache_result_t put_value(const CACHE_KEY& key,
                             const GWBUF* pValue);

    cache_result_t del_value(const CACHE_KEY& key);

private:
    friend class StorageFactory;

    StorageReal(CACHE_STORAGE_API* pApi, CACHE_STORAGE* pStorage);

    StorageReal(const StorageReal&);
    StorageReal& operator = (const StorageReal&);

private:
    CACHE_STORAGE_API* m_pApi;
    CACHE_STORAGE*     m_pStorage;
};
