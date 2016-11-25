#pragma once
#ifndef _MAXSCALE_FILTER_CACHE_STORAGE_H
#define _MAXSCALE_FILTER_CACHE_STORAGE_H
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
#include "cache_storage_api.h"

class Storage
{
public:
    ~Storage();

    cache_result_t getKey(const char* zDefaultDb,
                          const GWBUF* pQuery,
                          CACHE_KEY* pKey);

    cache_result_t getValue(const CACHE_KEY& key,
                            uint32_t flags,
                            GWBUF** ppValue);

    cache_result_t putValue(const CACHE_KEY& key,
                            const GWBUF* pValue);

    cache_result_t delValue(const CACHE_KEY& key);

private:
    friend class StorageFactory;

    Storage(CACHE_STORAGE_API* pApi, CACHE_STORAGE* pStorage);

    Storage(const Storage&);
    Storage& operator = (const Storage&);

private:
    CACHE_STORAGE_API* m_pApi;
    CACHE_STORAGE*     m_pStorage;
};

#endif
