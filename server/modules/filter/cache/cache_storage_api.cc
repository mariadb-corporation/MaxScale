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
#include "cache_storage_api.hh"
#include <ctype.h>
#include <sstream>

using std::string;
using std::stringstream;

std::string cache_key_to_string(const CACHE_KEY& key)
{
    stringstream ss;
    ss << key.data;

    return ss.str();
}

size_t cache_key_hash(const CACHE_KEY* key)
{
    mxb_assert(key);
    mxb_assert(sizeof(key->data) == sizeof(size_t));

    return key->data;
}

bool cache_key_equal_to(const CACHE_KEY* lhs, const CACHE_KEY* rhs)
{
    mxb_assert(lhs);
    mxb_assert(rhs);

    return lhs->data == rhs->data;
}
