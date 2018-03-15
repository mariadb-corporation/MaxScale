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
#include <maxscale/spinlock.hh>
#include "inmemorystorage.hh"

class InMemoryStorageMT : public InMemoryStorage
{
public:
    ~InMemoryStorageMT();

    typedef std::auto_ptr<InMemoryStorageMT> SInMemoryStorageMT;

    static SInMemoryStorageMT Create(const std::string& name,
                                     const CACHE_STORAGE_CONFIG& config,
                                     int argc, char* argv[]);

    cache_result_t get_info(uint32_t what, json_t** ppInfo) const;
    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags, uint32_t soft_ttl, uint32_t hard_ttl,
                             GWBUF** ppResult);
    cache_result_t put_value(const CACHE_KEY& key, const GWBUF& value);
    cache_result_t del_value(const CACHE_KEY& key);

private:
    InMemoryStorageMT(const std::string& name, const CACHE_STORAGE_CONFIG& config);

private:
    InMemoryStorageMT(const InMemoryStorageMT&);
    InMemoryStorageMT& operator = (const InMemoryStorageMT&);

private:
    mutable SPINLOCK m_lock;
};
