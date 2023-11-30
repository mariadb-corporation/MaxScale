/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "configuration.hh"
#include <fstream>
#include <maxscale/paths.hh>
#include <maxscale/secrets.hh>
#include <maxscale/key_manager.hh>
#include "protocolmodule.hh"

using namespace std;

namespace
{
namespace nosqlprotocol
{

// Use the module name as the configuration prefix
const char* CONFIG_PREFIX = MXB_MODULE_NAME;

mxs::config::Specification specification(MXB_MODULE_NAME, mxs::config::Specification::PROTOCOL,
                                         CONFIG_PREFIX);
}
}

// Can only be changed via MaxScale
mxs::config::ParamString Configuration::s_user(
    &nosqlprotocol::specification,
    "user",
    "The user to use when connecting to the backend.",
    "");

mxs::config::ParamPassword Configuration::s_password(
    &nosqlprotocol::specification,
    "password",
    "The password to use when connecting to the backend.",
    "");

mxs::config::ParamString Configuration::s_host(
    &nosqlprotocol::specification,
    "host",
    "The host to use when creating new users in the backend.",
    "%");

mxs::config::ParamBool Configuration::s_authentication_required(
    &nosqlprotocol::specification,
    "authentication_required",
    "Whether nosqlprotocol authentication is required.",
    false);

mxs::config::ParamBool Configuration::s_authentication_shared(
    &nosqlprotocol::specification,
    "authentication_shared",
    "Whether NoSQL credentials should be stored in the MariaDB server, thus enabling the "
    "use of several MaxScale instances with the same nosqlprotocol configuration.",
    false);

mxs::config::ParamString Configuration::s_authentication_db(
    &nosqlprotocol::specification,
    "authentication_db",
    "What database shared NoSQL user information should be stored in.",
    "nosqlprotocol");

mxs::config::ParamString Configuration::s_authentication_key_id(
    &nosqlprotocol::specification,
    "authentication_key_id",
    "If present and non-empty, and if 'authentication_shared' is enabled, then the sensitive "
    "parts of the NoSQL user data stored in the MariaDB server will be encrypted with this key ID.",
    "");

mxs::config::ParamString Configuration::s_authentication_user(
    &nosqlprotocol::specification,
    "authentication_user",
    "If 'authentication_shared' is enabled, this user should be used when storing the NoSQL "
    "user data to the MariaDB server.",
    "");

mxs::config::ParamPassword Configuration::s_authentication_password(
    &nosqlprotocol::specification,
    "authentication_password",
    "The password of the user specified with 'authentication_user'.",
    "");

mxs::config::ParamBool Configuration::s_authorization_enabled(
    &nosqlprotocol::specification,
    "authorization_enabled",
    "Whether nosqlprotocol authorization is enabled.",
    false);

mxs::config::ParamCount Configuration::s_id_length(
    &nosqlprotocol::specification,
    "id_length",
    "The VARCHAR length of automatically created tables. A changed value only affects "
    "tables created after the change; existing tables are not altered.",
    Configuration::ID_LENGTH_DEFAULT,
    Configuration::ID_LENGTH_MIN,
    Configuration::ID_LENGTH_MAX);

// Can be changed from the NosQL API.
mxs::config::ParamBool Configuration::s_auto_create_databases(
    &nosqlprotocol::specification,
    "auto_create_databases",
    "Whether databases should be created automatically. If enabled, whenever a document is "
    "inserted to a collection the corresponding database will automatically be created if "
    "it does not exist already.",
    true);

mxs::config::ParamBool Configuration::s_auto_create_tables(
    &nosqlprotocol::specification,
    "auto_create_tables",
    "Whether tables should be created automatically. If enabled, whenever a document is "
    "inserted to a collection the corresponding table will automatically be created if "
    "it does not exist already.",
    true);

mxs::config::ParamEnumMask<Configuration::Debug> Configuration::s_debug(
    &nosqlprotocol::specification,
    "debug",
    "To what extent debugging logging should be performed.",
    {
        {Configuration::DEBUG_NONE, "none"},
        {Configuration::DEBUG_IN, "in"},
        {Configuration::DEBUG_OUT, "out"},
        {Configuration::DEBUG_BACK, "back"}
    },
    0);

mxs::config::ParamSeconds Configuration::s_cursor_timeout(
    &nosqlprotocol::specification,
    "cursor_timeout",
    "How long can a cursor be idle, that is, not accessed, before it is automatically closed.",
    std::chrono::seconds(Configuration::CURSOR_TIMEOUT_DEFAULT));

mxs::config::ParamBool Configuration::s_log_unknown_command(
    &nosqlprotocol::specification,
    "log_unknown_command",
    "Whether an unknown command should be logged.",
    false);

mxs::config::ParamEnum<Configuration::OnUnknownCommand> Configuration::s_on_unknown_command(
    &nosqlprotocol::specification,
    "on_unknown_command",
    "Whether to return an error or an empty document in case an unknown NoSQL "
    "command is encountered.",
    {
        {Configuration::RETURN_ERROR, "return_error"},
        {Configuration::RETURN_EMPTY, "return_empty"}
    },
    Configuration::RETURN_ERROR);

mxs::config::ParamEnum<Configuration::OrderedInsertBehavior> Configuration::s_ordered_insert_behavior(
    &nosqlprotocol::specification,
    "ordered_insert_behavior",
    "Whether documents will be inserted in a way true to how NoSQL behaves, "
    "or in a way that is efficient from MariaDB's point of view.",
    {
        {Configuration::OrderedInsertBehavior::DEFAULT, "default"},
        {Configuration::OrderedInsertBehavior::ATOMIC, "atomic"}
    },
    Configuration::OrderedInsertBehavior::DEFAULT);


Configuration::Configuration(const std::string& name, ProtocolModule* pInstance)
    : mxs::config::Configuration(name, &nosqlprotocol::specification)
    , m_instance(*pInstance)
{
    add_native(&Configuration::user, &s_user);
    add_native(&Configuration::password, &s_password);
    add_native(&Configuration::host, &s_host);
    add_native(&Configuration::authentication_required, &s_authentication_required);
    add_native(&Configuration::authentication_shared, &s_authentication_shared);
    add_native(&Configuration::authentication_db, &s_authentication_db);
    add_native(&Configuration::authentication_key_id, &s_authentication_key_id);
    add_native(&Configuration::authentication_user, &s_authentication_user);
    add_native(&Configuration::authentication_password, &s_authentication_password);
    add_native(&Configuration::authorization_enabled, &s_authorization_enabled);
    add_native(&Configuration::id_length, &s_id_length);

    add_native(&Configuration::auto_create_databases, &s_auto_create_databases);
    add_native(&Configuration::auto_create_tables, &s_auto_create_tables);
    add_native(&Configuration::cursor_timeout, &s_cursor_timeout);
    add_native(&Configuration::debug, &s_debug);
    add_native(&Configuration::log_unknown_command, &s_log_unknown_command);
    add_native(&Configuration::on_unknown_command, &s_on_unknown_command);
    add_native(&Configuration::ordered_insert_behavior, &s_ordered_insert_behavior);
}

bool Configuration::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    bool rv = true;

    if (this->authentication_shared)
    {
        if (this->authentication_user.empty() || this->authentication_password.empty())
        {
            MXB_ERROR("If 'authentication_shared' is true, then 'authentication_user' and "
                      "'authentication_password' must be specified.");
            rv = false;
        }
        else if (!this->authentication_key_id.empty())
        {
            if (auto km = mxs::key_manager(); !km)
            {
                MXB_ERROR("The 'key_manager' has not been configured, cannot retrieve encryption keys");
                rv = false;
            }
            else if (auto [ok, version, key] = km->get_key(this->authentication_key_id); !ok)
            {
                MXB_ERROR("Failed to retrieve encryption key.");
                rv = false;
            }
            else if (key.size() != mxs::SECRETS_CIPHER_BYTES)
            {
                MXB_ERROR("Configured encryption key is not a 256-bit key.");
                rv = false;
            }
            else
            {
                this->encryption_key = std::move(key);
                this->encryption_key_version = version;
            }
        }
    }

    if (rv)
    {
        rv = m_instance.post_configure();
    }

    return rv;
}

// static
mxs::config::Specification& Configuration::specification()
{
    return nosqlprotocol::specification;
}
