/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "storagereal.hh"


StorageReal::StorageReal(CACHE_STORAGE_API* pApi, CACHE_STORAGE* pStorage)
    : m_pApi(pApi)
    , m_pStorage(pStorage)
{
    ss_dassert(m_pApi);
    ss_dassert(m_pStorage);
}

StorageReal::~StorageReal()
{
    m_pApi->freeInstance(m_pStorage);
}

void StorageReal::get_config(CACHE_STORAGE_CONFIG* pConfig)
{
    m_pApi->getConfig(m_pStorage, pConfig);
}

cache_result_t StorageReal::get_info(uint32_t flags, json_t** ppInfo) const
{
    return m_pApi->getInfo(m_pStorage, flags, ppInfo);
}

cache_result_t StorageReal::get_value(const CACHE_KEY& key,
                                      uint32_t flags,
                                      uint32_t soft_ttl,
                                      uint32_t hard_ttl,
                                      GWBUF** ppValue) const
{
    return m_pApi->getValue(m_pStorage, &key, flags, soft_ttl, hard_ttl, ppValue);
}

cache_result_t StorageReal::put_value(const CACHE_KEY& key, const GWBUF* pValue)
{
    return m_pApi->putValue(m_pStorage, &key, pValue);
}

cache_result_t StorageReal::del_value(const CACHE_KEY& key)
{
    return m_pApi->delValue(m_pStorage, &key);
}

cache_result_t StorageReal::get_head(CACHE_KEY* pKey, GWBUF** ppHead) const
{
    return m_pApi->getHead(m_pStorage, pKey, ppHead);
}

cache_result_t StorageReal::get_tail(CACHE_KEY* pKey, GWBUF** ppTail) const
{
    return m_pApi->getTail(m_pStorage, pKey, ppTail);
}

cache_result_t StorageReal::get_size(uint64_t* pSize) const
{
    return m_pApi->getSize(m_pStorage, pSize);
}

cache_result_t StorageReal::get_items(uint64_t* pItems) const
{
    return m_pApi->getItems(m_pStorage, pItems);
}
