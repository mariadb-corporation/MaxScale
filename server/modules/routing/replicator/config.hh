/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <string>

#include <maxscale/service.hh>
#include <maxbase/regex.hh>

namespace cdc
{

struct Server
{
    std::string host;       // Address to connect to
    int         port;       // Port where the server is listening
    std::string user;       // Username used for the connection
    std::string password;   // Password for the user
};

struct Config
{
    int         server_id = 1234;   // Server ID used in registration
    std::string gtid;               // Starting GTID
    SERVICE*    service;
    std::string statedir = ".";
    pcre2_code* match = nullptr;
    pcre2_code* exclude = nullptr;
    int         timeout = 10;
};
}
