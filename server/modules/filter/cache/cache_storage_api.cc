/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cache_storage_api.hh"
#include <ctype.h>
#include <sstream>

using std::map;
using std::string;
using std::stringstream;
using std::vector;

const char CN_STORAGE_ARG_SERVER[] = "server";

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

bool Storage::split_arguments(const std::string& argument_string,
                              map<std::string, std::string>* pArguments)
{
    bool rv = true;

    vector<string> arguments = mxb::strtok(argument_string, ",");
    map<string, string> values_by_keys;

    for (const auto& argument : arguments)
    {
        vector<string> key_value = mxb::strtok(argument, "=");

        switch (key_value.size())
        {
        case 1:
            values_by_keys[mxb::trimmed_copy(key_value[0])] = "";
            break;

        case 2:
            values_by_keys[mxb::trimmed_copy(key_value[0])] = mxb::trimmed_copy(key_value[1]);
            break;

        default:
            MXS_ERROR("The provided argument string '%s' is not of the correct format.",
                      argument_string.c_str());
            rv = false;
        }
    }

    if (rv)
    {
        pArguments->swap(values_by_keys);
    }

    return rv;
}

bool Storage::get_host(const std::string& s, int default_port, mxb::Host* pHost)
{
    mxb::Host host = mxb::Host::from_string(s, default_port);

    bool valid = host.is_valid();

    if (valid)
    {
        *pHost = host;
    }
    else
    {
        MXS_ERROR("The provided value '%s' is not valid.", s.c_str());
    }

    return valid;
}
