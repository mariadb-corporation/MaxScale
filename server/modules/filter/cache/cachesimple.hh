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
#include <tr1/unordered_map>
#include "cache.hh"
#include "cache_storage_api.hh"

class Storage;

class CacheSimple : public Cache
{
public:
    ~CacheSimple();

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags, uint32_t soft_ttl, uint32_t hard_ttl,
                             GWBUF** ppValue) const;

    cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pValue);

    cache_result_t del_value(const CACHE_KEY& key);

protected:
    CacheSimple(const std::string&              name,
                const CACHE_CONFIG*             pConfig,
                const std::vector<SCacheRules>& Rules,
                SStorageFactory                 sFactory,
                Storage*                        pStorage);

    static bool Create(const CACHE_CONFIG&       config,
                       std::vector<SCacheRules>* pRules,
                       StorageFactory**          ppFactory);


    json_t* do_get_info(uint32_t what) const;

    bool do_must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession);

    void do_refreshed(const CACHE_KEY& key, const CacheFilterSession* pSession);

private:
    CacheSimple(const Cache&);
    CacheSimple& operator = (const CacheSimple&);

protected:
    typedef std::tr1::unordered_map<CACHE_KEY, const CacheFilterSession*> Pending;

    Pending  m_pending;  // Pending items; being fetched from the backend.
    Storage* m_pStorage; // The storage instance to use.
};
