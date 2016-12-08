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
#include <functional>
#include <string>
#include <tr1/functional>
#include "cache_storage_api.h"


namespace std
{

template<>
struct equal_to<CACHE_KEY>
{
    bool operator()(const CACHE_KEY& lhs, const CACHE_KEY& rhs) const
    {
        return cache_key_equal_to(&lhs, &rhs);
    }
};

namespace tr1
{

template<>
struct hash<CACHE_KEY>
{
    size_t operator()(const CACHE_KEY& key) const
    {
        return cache_key_hash(&key);
    }
};

}

}

std::string cache_key_to_string(const CACHE_KEY& key);

