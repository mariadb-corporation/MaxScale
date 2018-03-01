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
#include "inmemorystorage.hh"

class InMemoryStorageST : public InMemoryStorage
{
public:
    ~InMemoryStorageST();

    typedef std::auto_ptr<InMemoryStorageST> SInMemoryStorageST;

    static SInMemoryStorageST Create(const std::string& name,
                                     const CACHE_STORAGE_CONFIG& config,
                                     int argc, char* argv[]);

    cache_result_t get_info(uint32_t what, json_t** ppInfo) const;
    cache_result_t get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppResult);
    cache_result_t put_value(const CACHE_KEY& key, const GWBUF& pValue);
    cache_result_t del_value(const CACHE_KEY& key);

private:
    InMemoryStorageST(const std::string& name, const CACHE_STORAGE_CONFIG& config);

private:
    InMemoryStorageST(const InMemoryStorageST&);
    InMemoryStorageST& operator = (const InMemoryStorageST&);
};
