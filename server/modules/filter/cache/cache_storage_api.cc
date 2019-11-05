/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
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
    ss << "{ ";
    ss << "user: " << "\"" << key.user << "\", ";
    ss << "host: " << "\"" << key.host << "\", ";
    ss << "data_hash: " << key.data_hash << ",";
    ss << "full_hash: " << key.full_hash;
    ss << " }";

    return ss.str();
}

size_t cache_key_hash(const CACHE_KEY& key)
{
    return key.full_hash;
}

bool cache_key_equal_to(const CACHE_KEY& lhs, const CACHE_KEY& rhs)
{
    return
        lhs.full_hash == rhs.full_hash
        && lhs.data_hash == rhs.data_hash
        && lhs.user == rhs.user
        && lhs.host == rhs.host;
}
