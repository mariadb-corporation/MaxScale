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

#define MXS_MODULE_NAME "cache"
#include "storage.h"


Storage::Storage(CACHE_STORAGE_API* pApi, CACHE_STORAGE* pStorage)
    : m_pApi(pApi)
    , m_pStorage(pStorage)
{
    ss_dassert(m_pApi);
    ss_dassert(m_pStorage);
}

Storage::~Storage()
{
}

cache_result_t Storage::get_key(const char* zDefaultDb,
                                const GWBUF* pQuery,
                                CACHE_KEY* pKey)
{
    return m_pApi->getKey(m_pStorage, zDefaultDb, pQuery, pKey);
}

cache_result_t Storage::get_value(const CACHE_KEY& key,
                                  uint32_t flags,
                                  GWBUF** ppValue)
{
    return m_pApi->getValue(m_pStorage, &key, flags, ppValue);
}

cache_result_t Storage::put_value(const CACHE_KEY& key,
                                  const GWBUF* pValue)
{
    return m_pApi->putValue(m_pStorage, &key, pValue);
}

cache_result_t Storage::del_value(const CACHE_KEY& key)
{
    return m_pApi->delValue(m_pStorage, &key);
}
