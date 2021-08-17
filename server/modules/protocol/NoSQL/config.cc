/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "config.hh"
#include "protocolmodule.hh"

namespace
{
namespace nosqlprotocol
{

// Use the module name as the configuration prefix
const char* CONFIG_PREFIX = MXS_MODULE_NAME;

mxs::config::Specification specification(MXS_MODULE_NAME, mxs::config::Specification::PROTOCOL,
                                         CONFIG_PREFIX);
}
}

mxs::config::ParamString GlobalConfig::s_user(
    &nosqlprotocol::specification,
    "user",
    "The user to use when connecting to the backend.");

mxs::config::ParamString GlobalConfig::s_password(
    &nosqlprotocol::specification,
    "password",
    "The password to use when connecting to the backend.");

mxs::config::ParamEnum<GlobalConfig::OnUnknownCommand> GlobalConfig::s_on_unknown_command(
    &nosqlprotocol::specification,
    "on_unknown_command",
    "Whether to return an error or an empty document in case an unknown NoSQL "
    "command is encountered.",
{
    {GlobalConfig::RETURN_ERROR, "return_error"},
    {GlobalConfig::RETURN_EMPTY, "return_empty"}
},
    GlobalConfig::RETURN_ERROR);

mxs::config::ParamBool GlobalConfig::s_auto_create_databases(
    &nosqlprotocol::specification,
    "auto_create_databases",
    "Whether databases should be created automatically. If enabled, whenever a document is "
    "inserted to a collection the corresponding database will automatically be created if "
    "it does not exist already.",
    true);

mxs::config::ParamBool GlobalConfig::s_auto_create_tables(
    &nosqlprotocol::specification,
    "auto_create_tables",
    "Whether tables should be created automatically. If enabled, whenever a document is "
    "inserted to a collection the corresponding table will automatically be created if "
    "it does not exist already.",
    true);

mxs::config::ParamCount GlobalConfig::s_id_length(
    &nosqlprotocol::specification,
    "id_length",
    "The VARCHAR length of automatically created tables. A changed value only affects "
    "tables created after the change; existing tables are not altered.",
    GlobalConfig::ID_LENGTH_DEFAULT,
    GlobalConfig::ID_LENGTH_MIN,
    GlobalConfig::ID_LENGTH_MAX);

mxs::config::ParamEnum<GlobalConfig::OrderedInsertBehavior> GlobalConfig::s_ordered_insert_behavior(
    &nosqlprotocol::specification,
    "ordered_insert_behavior",
    "Whether documents will be inserted in a way true to how NoSQL behaves, "
    "or in a way that is efficient from MariaDB's point of view.",
{
    {GlobalConfig::OrderedInsertBehavior::DEFAULT, "default"},
    {GlobalConfig::OrderedInsertBehavior::ATOMIC, "atomic"}
},
    GlobalConfig::OrderedInsertBehavior::DEFAULT);

mxs::config::ParamSeconds GlobalConfig::s_cursor_timeout(
    &nosqlprotocol::specification,
    "cursor_timeout",
    "How long can a cursor be idle, that is, not accessed, before it is automatically closed.",
    mxs::config::NO_INTERPRETATION,
    std::chrono::seconds(GlobalConfig::CURSOR_TIMEOUT_DEFAULT));

mxs::config::ParamBool GlobalConfig::s_log_unknown_command(
    &nosqlprotocol::specification,
    "log_unknown_command",
    "Whether an unknown command should be logged.",
    false);


GlobalConfig::GlobalConfig(const std::string& name, ProtocolModule* instance)
    : mxs::config::Configuration(name, &nosqlprotocol::specification)
    , m_instance(instance)
{
    add_native(&GlobalConfig::user, &s_user);
    add_native(&GlobalConfig::password, &s_password);
    add_native(&GlobalConfig::on_unknown_command, &s_on_unknown_command);
    add_native(&GlobalConfig::auto_create_databases, &s_auto_create_databases);
    add_native(&GlobalConfig::auto_create_tables, &s_auto_create_tables);
    add_native(&GlobalConfig::id_length, &s_id_length);
    add_native(&GlobalConfig::ordered_insert_behavior, &s_ordered_insert_behavior);
    add_native(&GlobalConfig::cursor_timeout, &s_cursor_timeout);
    add_native(&GlobalConfig::log_unknown_command, &s_log_unknown_command);
}

bool GlobalConfig::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    m_instance->post_configure();
    return true;
}

// static
mxs::config::Specification& GlobalConfig::specification()
{
    return nosqlprotocol::specification;
}
