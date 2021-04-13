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

}
}

mxs::config::ParamString Config::s_user(
    &mongodbclient::specification,
    "user",
    "The user to use when connecting to the backend.");

mxs::config::ParamString Config::s_password(
    &mongodbclient::specification,
    "password",
    "The password to use when connecting to the backend.");

mxs::config::ParamEnum<Config::OnUnknownCommand> Config::s_on_unknown_command(
    &mongodbclient::specification,
    "on_unknown_command",
    "Whether to return an error or an empty document in case an unknown Mongo "
    "command is encountered.",
    {
        { Config::RETURN_ERROR, "return_error" },
        { Config::RETURN_EMPTY, "return_empty" }
    },
    Config::RETURN_ERROR);

mxs::config::ParamBool Config::s_auto_create_tables(
    &mongodbclient::specification,
    "auto_create_tables",
    "Whether tables should be created automatically. If enabled, whenever a document is "
    "inserted to a collection the corresponding table will automatically be created if "
    "it does not exist already.",
    true);

mxs::config::ParamCount Config::s_id_length(
    &mongodbclient::specification,
    "id_length",
    "The VARCHAR length of automatically created tables. A changed value only affects "
    "tables created after the change; existing tables are not altered.",
    Config::ID_LENGTH_DEFAULT,
    Config::ID_LENGTH_MIN,
    Config::ID_LENGTH_MAX
    );


Config::Config()
    : mxs::config::Configuration(MXS_MODULE_NAME, &mongodbclient::specification)
{
    add_native(&Config::user, &s_user);
    add_native(&Config::password, &s_password);
    add_native(&Config::on_unknown_command, &s_on_unknown_command);
    add_native(&Config::auto_create_tables, &s_auto_create_tables);
    add_native(&Config::id_length, &s_id_length);
}

//static
mxs::config::Specification& Config::specification()
{
    return mongodbclient::specification;
}
