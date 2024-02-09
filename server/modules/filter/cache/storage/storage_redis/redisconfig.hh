/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "storage_redis.hh"
#include <maxscale/config2.hh>

class RedisConfig : public mxs::config::Configuration
{
public:
    static constexpr char DEFAULT_ADDRESS[] = "127.0.0.1";
    static const int DEFAULT_PORT = 6379;

    RedisConfig(const std::string& name);
    RedisConfig(RedisConfig&& other) = default;

    mxb::Host   host;
    std::string username;
    std::string password;
    bool        ssl;
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_ca;

    static const mxs::config::Specification& specification();
};
