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

#include "config.hh"

namespace
{
namespace mongodbclient
{

mxs::config::Specification specification(MXS_MODULE_NAME, mxs::config::Specification::PROTOCOL);

mxs::config::ParamString user(
    &specification,
    "user",
    "The user to use when connecting to the backend.");

mxs::config::ParamString password(
    &specification,
    "password",
    "The password to use when connecting to the backend.");
}
}

Config::Config()
    : mxs::config::Configuration(MXS_MODULE_NAME, &mongodbclient::specification)
{
    add_native(&Config::user, &mongodbclient::user);
    add_native(&Config::password, &mongodbclient::password);
}

//static
mxs::config::Specification& Config::specification()
{
    return mongodbclient::specification;
}
