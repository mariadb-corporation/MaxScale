/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <vector>

namespace parser
{
struct MasterConfig
{
    std::string host;
    int         port = 0;
    std::string user;
    std::string password;
    bool        use_gtid = false;

    bool        ssl = false;
    std::string ssl_ca;
    std::string ssl_capath;
    std::string ssl_cert;
    std::string ssl_crl;
    std::string ssl_crlpath;
    std::string ssl_key;
    std::string ssl_cipher;
    bool        ssl_verify_server_cert;
};

struct Handler
{
    virtual void select(const std::vector<std::string>& values) = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;

    virtual void change_master_to(const MasterConfig& config) = 0;
    virtual void start_slave() = 0;
    virtual void stop_slave() = 0;
    virtual void reset_slave() = 0;
    virtual void show_slave_status() = 0;

    virtual void flush_logs() = 0;
    virtual void purge_logs() = 0;

    virtual void error(const std::string& err) = 0;
};

void parse(const std::string& line, Handler* handler);
}
