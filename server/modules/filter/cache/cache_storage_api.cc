/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "cache"
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

bool Storage::parse_argument_string(const std::string& argument_string,
                                    mxs::ConfigParameters* pParameters)
{
    bool rv = true;

    vector<string> arguments = mxb::strtok(argument_string, ",");
    mxs::ConfigParameters parameters;

    for (const auto& argument : arguments)
    {
        vector<string> key_value = mxb::strtok(argument, "=");

        switch (key_value.size())
        {
        case 2:
            parameters.set(mxb::trimmed_copy(key_value[0]), mxb::trimmed_copy(key_value[1]));
            break;

        default:
            MXB_ERROR("The provided argument string '%s' is not of the correct format.",
                      argument_string.c_str());
            rv = false;
        }
    }

    if (rv)
    {
        pParameters->swap(parameters);
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
        MXB_ERROR("The provided value '%s' is not valid.", s.c_str());
    }

    return valid;
}
