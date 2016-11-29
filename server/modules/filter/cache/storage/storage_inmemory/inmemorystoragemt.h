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
#include <maxscale/spinlock.h>
#include "inmemorystorage.h"

class InMemoryStorageMT : public InMemoryStorage
{
public:
    ~InMemoryStorageMT();

    static InMemoryStorageMT* create(const std::string& name, uint32_t ttl, int argc, char* argv[]);

    cache_result_t get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppresult);
    cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pvalue);
    cache_result_t del_value(const CACHE_KEY& key);

private:
    InMemoryStorageMT(const std::string& name, uint32_t ttl);

private:
    InMemoryStorageMT(const InMemoryStorageMT&);
    InMemoryStorageMT& operator = (const InMemoryStorageMT&);

private:
    SPINLOCK lock_;
};
