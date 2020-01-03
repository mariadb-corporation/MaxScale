/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-12-18
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
using std::vector;

std::string CacheKey::to_string() const
{
    stringstream ss;
    ss << "{ ";
    ss << "user: " << "\"" << this->user << "\", ";
    ss << "host: " << "\"" << this->host << "\", ";
    ss << "data_hash: " << this->data_hash << ",";
    ss << "full_hash: " << this->full_hash;
    ss << " }";

    return ss.str();
}

vector<char> CacheKey::to_vector() const
{
    vector<char> rv;
    rv.reserve(this->user.size() + this->host.size() + sizeof(uint64_t) + sizeof(uint64_t));

    auto it = std::back_inserter(rv);

    const char* p;

    p = this->user.c_str();
    std::copy(p, p + this->user.size(), it);
    p = this->host.c_str();
    std::copy(p, p + this->host.size(), it);
    p = reinterpret_cast<const char*>(&this->data_hash);
    std::copy(p, p + sizeof(this->data_hash), it);
    p = reinterpret_cast<const char*>(&this->full_hash);
    std::copy(p, p + sizeof(this->full_hash), it);

    return rv;
}

bool CacheKey::eq(const CacheKey& that) const
{
    return
        this->full_hash == that.full_hash
        && this->data_hash == that.data_hash
        && this->user == that.user
        && this->host == that.host;
}
