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
