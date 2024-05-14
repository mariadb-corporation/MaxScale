/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "storage_memcached.hh"
#include <maxscale/config2.hh>

class MemcachedConfig : public mxs::config::Configuration
{
public:
    static constexpr char DEFAULT_ADDRESS[] = "127.0.0.1";
    static const int DEFAULT_PORT = 11211;
    static const int DEFAULT_MAX_VALUE_SIZE = 1 * 1024 * 1024;

    MemcachedConfig(const std::string& name);
    MemcachedConfig(MemcachedConfig&& other) = default;

    mxb::Host   host;
    int64_t     max_value_size;

    static const mxs::config::Specification& specification();
};
