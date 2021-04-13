/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "mongodbclient.hh"
#include <maxscale/config2.hh>

class Config : public mxs::config::Configuration
{
public:
    Config();
    Config(Config&&) = default;

    enum OnUnknownCommand
    {
        RETURN_ERROR,
        RETURN_EMPTY,
    };

    enum
    {
        ID_LENGTH_DEFAULT = 24,
        ID_LENGTH_MIN     = 24,
        ID_LENGTH_MAX     = 2048,
    };

    std::string      user;
    std::string      password;
    OnUnknownCommand on_unknown_command { RETURN_ERROR };
    bool             auto_create_tables { true };
    int64_t          id_length          { ID_LENGTH_DEFAULT };

    static mxs::config::Specification& specification();
};
