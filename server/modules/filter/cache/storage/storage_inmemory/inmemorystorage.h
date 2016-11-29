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
#include <memory>
#include <string>
#include <vector>
#include <tr1/unordered_map>
#include "../../cachefilter.h"

class InMemoryStorage
{
public:
    virtual ~InMemoryStorage();

    cache_result_t get_key(const char* zdefault_db, const GWBUF* pquery, CACHE_KEY* pkey);

    virtual cache_result_t get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppresult) = 0;
    virtual cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pvalue) = 0;
    virtual cache_result_t del_value(const CACHE_KEY& key) = 0;

protected:
    InMemoryStorage(const std::string& name, uint32_t ttl);

    cache_result_t do_get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppresult);
    cache_result_t do_put_value(const CACHE_KEY& key, const GWBUF* pvalue);
    cache_result_t do_del_value(const CACHE_KEY& key);

private:
    InMemoryStorage(const InMemoryStorage&);
    InMemoryStorage& operator = (const InMemoryStorage&);

private:
    typedef std::vector<uint8_t> Value;

    struct Entry
    {
        Entry()
        : time(0)
        {}

        uint32_t time;
        Value    value;
    };

    typedef std::tr1::unordered_map<CACHE_KEY, Entry> Entries;

    std::string name_;
    uint32_t    ttl_;
    Entries     entries_;
};
