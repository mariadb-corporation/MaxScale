/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "configuration.hh"
#include "nosqlconfig.hh"
#include <fstream>
#include <maxscale/paths.hh>
#include <maxscale/secrets.hh>
#include <maxscale/key_manager.hh>
#include "../../filter/cache/cacheconfig.hh"
#include "protocolmodule.hh"

using namespace std;

namespace
{

using mxs::config::ConcreteParam;

/*
 * ParamInternalCache
 *
 * This class allows the specification of one module to appear as a nested configuration
 * of another. This will appear as:
 *
 * [NoSQL-Listener]
 * type=listener
 * protocol=nosqlprotocol
 * nosqlprotocol.internal_cache=cache
 * nosqlprotocol.cache.max_size=1M
 * ...
 *
 * Currently hardwired for the needs of 'nosqlprotocol' but could be generalized if needed.
 */
class ParamInternalCache : public ConcreteParam<ParamInternalCache, const mxs::config::Specification*>
{
public:
    ParamInternalCache(mxs::config::Specification* pSpecification,
                       const char* zName,
                       const char* zDescription,
                       const mxs::config::Specification* pInternal_cache)
        : ConcreteParam<ParamInternalCache, const mxs::config::Specification*>(
            pSpecification, zName, zDescription, Param::AT_STARTUP, Param::OPTIONAL, nullptr)
        , m_pInternal_cache(pInternal_cache)
    {
    }

    std::string type() const override;

    bool takes_parameters() const override;

    std::string parameter_prefix(const std::string& value) const override;

    bool validate_parameters(const std::string& value,
                             const mxs::ConfigParameters& params,
                             mxs::ConfigParameters* pUnrecognized = nullptr) const override;

    bool validate_parameters(const std::string& value,
                             json_t* pParams,
                             std::set<std::string>* pUnrecognized = nullptr) const override;

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

private:
    const mxs::config::Specification* m_pInternal_cache;
};


std::string ParamInternalCache::type() const
{
    return "string";
}

bool ParamInternalCache::takes_parameters() const
{
    return true;
}

std::string ParamInternalCache::parameter_prefix(const std::string& value) const
{
    return m_pInternal_cache->module();
}

bool ParamInternalCache::validate_parameters(const std::string& value,
                                             const mxs::ConfigParameters& params,
                                             mxs::ConfigParameters* pUnrecognized) const
{
    bool valid = (value == m_pInternal_cache->module());

    if (valid)
    {
        string storage = params.get_string("storage");
        const string_view STORAGE_INMEMORY = "storage_inmemory";

        if (storage.empty() || storage == STORAGE_INMEMORY)
        {
            valid = m_pInternal_cache->validate(params, pUnrecognized);
        }
        else
        {
            MXB_ERROR("'%s' specified as storage of the internal cache of nosqlprotocol, "
                      "but only '%.*s' can be used.",
                      storage.c_str(),
                      (int)STORAGE_INMEMORY.length(), STORAGE_INMEMORY.data());
            valid = false;
        }
    }

    return valid;
}

bool ParamInternalCache::validate_parameters(const std::string& value,
                                             json_t* pParams,
                                             std::set<std::string>* pUnrecognized) const
{
    bool valid = (value == m_pInternal_cache->module());

    if (valid)
    {
        valid = m_pInternal_cache->validate(pParams, pUnrecognized);
    }

    return valid;
}

std::string ParamInternalCache::to_string(value_type value) const
{
    return value ? value->module() : "";
}

bool ParamInternalCache::from_string(const std::string& value_as_string,
                                     value_type* pValue,
                                     std::string* pMessage) const
{
    bool rv = false;

    if (value_as_string.empty())
    {
        *pValue = nullptr;
        rv = true;
    }
    else if (value_as_string == m_pInternal_cache->module())
    {
        *pValue = m_pInternal_cache;
        rv = true;
    }
    else if (pMessage)
    {
        *pMessage = "'";
        *pMessage += value_as_string;
        *pMessage += "' is not '";
        *pMessage += m_pInternal_cache->module();
        *pMessage += "'.";
    }

    return rv;
}

json_t* ParamInternalCache::to_json(value_type value) const
{
    return value ? json_string(value->module().c_str()) : json_null();
}

bool ParamInternalCache::from_json(const json_t* pJson, value_type* pValue, std::string* pMessage) const
{
    bool rv = false;

    if (json_is_string(pJson))
    {
        const char* z = json_string_value(pJson);

        rv = from_string(z, pValue, pMessage);
    }
    else if (pMessage)
    {
        *pMessage = "Expected a json string, but got a json ";
        *pMessage += mxb::json_type_to_string(pJson);
        *pMessage += ".";
    }

    return rv;
}
}

namespace
{
namespace nosqlprotocol
{

// Use the module name as the configuration prefix
const char* CONFIG_PREFIX = MXB_MODULE_NAME;

mxs::config::Specification specification(MXB_MODULE_NAME, mxs::config::Specification::PROTOCOL,
                                         CONFIG_PREFIX);

// Can only be changed via MaxScale
mxs::config::ParamString user(
    &nosqlprotocol::specification,
    "user",
    "The user to use when connecting to the backend.",
    "");

mxs::config::ParamPassword password(
    &nosqlprotocol::specification,
    "password",
    "The password to use when connecting to the backend.",
    "");

mxs::config::ParamString host(
    &nosqlprotocol::specification,
    "host",
    "The host to use when creating new users in the backend.",
    "%");

mxs::config::ParamBool authentication_required(
    &nosqlprotocol::specification,
    "authentication_required",
    "Whether nosqlprotocol authentication is required.",
    false);

mxs::config::ParamBool authentication_shared(
    &nosqlprotocol::specification,
    "authentication_shared",
    "Whether NoSQL credentials should be stored in the MariaDB server, thus enabling the "
    "use of several MaxScale instances with the same nosqlprotocol configuration.",
    false);

mxs::config::ParamString authentication_db(
    &nosqlprotocol::specification,
    "authentication_db",
    "What database shared NoSQL user information should be stored in.",
    "nosqlprotocol");

mxs::config::ParamString authentication_key_id(
    &nosqlprotocol::specification,
    "authentication_key_id",
    "If present and non-empty, and if 'authentication_shared' is enabled, then the sensitive "
    "parts of the NoSQL user data stored in the MariaDB server will be encrypted with this key ID.",
    "");

mxs::config::ParamString authentication_user(
    &nosqlprotocol::specification,
    "authentication_user",
    "If 'authentication_shared' is enabled, this user should be used when storing the NoSQL "
    "user data to the MariaDB server.",
    "");

mxs::config::ParamPassword authentication_password(
    &nosqlprotocol::specification,
    "authentication_password",
    "The password of the user specified with 'authentication_user'.",
    "");

mxs::config::ParamBool authorization_enabled(
    &nosqlprotocol::specification,
    "authorization_enabled",
    "Whether nosqlprotocol authorization is enabled.",
    false);

mxs::config::ParamCount id_length(
    &nosqlprotocol::specification,
    "id_length",
    "The VARCHAR length of automatically created tables. A changed value only affects "
    "tables created after the change; existing tables are not altered.",
    Configuration::ID_LENGTH_DEFAULT,
    Configuration::ID_LENGTH_MIN,
    Configuration::ID_LENGTH_MAX);

// Can be changed from the NosQL API.
mxs::config::ParamBool auto_create_databases(
    &nosqlprotocol::specification,
    "auto_create_databases",
    "Whether databases should be created automatically. If enabled, whenever a document is "
    "inserted to a collection the corresponding database will automatically be created if "
    "it does not exist already.",
    true);

mxs::config::ParamBool auto_create_tables(
    &nosqlprotocol::specification,
    "auto_create_tables",
    "Whether tables should be created automatically. If enabled, whenever a document is "
    "inserted to a collection the corresponding table will automatically be created if "
    "it does not exist already.",
    true);

mxs::config::ParamEnumMask<Configuration::Debug> debug(
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

mxs::config::ParamSeconds cursor_timeout(
    &nosqlprotocol::specification,
    "cursor_timeout",
    "How long can a cursor be idle, that is, not accessed, before it is automatically closed.",
    std::chrono::seconds(Configuration::CURSOR_TIMEOUT_DEFAULT));

mxs::config::ParamBool log_unknown_command(
    &nosqlprotocol::specification,
    "log_unknown_command",
    "Whether an unknown command should be logged.",
    false);

mxs::config::ParamEnum<Configuration::OnUnknownCommand> on_unknown_command(
    &nosqlprotocol::specification,
    "on_unknown_command",
    "Whether to return an error or an empty document in case an unknown NoSQL "
    "command is encountered.",
    {
        {Configuration::RETURN_ERROR, "return_error"},
        {Configuration::RETURN_EMPTY, "return_empty"}
    },
    Configuration::RETURN_ERROR);

mxs::config::ParamEnum<Configuration::OrderedInsertBehavior> ordered_insert_behavior(
    &nosqlprotocol::specification,
    "ordered_insert_behavior",
    "Whether documents will be inserted in a way true to how NoSQL behaves, "
    "or in a way that is efficient from MariaDB's point of view.",
    {
        {Configuration::OrderedInsertBehavior::DEFAULT, "default"},
        {Configuration::OrderedInsertBehavior::ATOMIC, "atomic"}
    },
    Configuration::OrderedInsertBehavior::DEFAULT);

ParamInternalCache internal_cache(
    &nosqlprotocol::specification,
    "internal_cache",
    "Which, if any, cache the nosql protocol should use. Currently only 'cache' is available.",
    CacheConfig::specification());
}
}


Configuration::Configuration(const std::string& name, ProtocolModule* pInstance)
    : mxs::config::Configuration(name, &nosqlprotocol::specification)
    , m_instance(*pInstance)
{
    add_native(&Configuration::user, &nosqlprotocol::user);
    add_native(&Configuration::password, &nosqlprotocol::password);
    add_native(&Configuration::host, &nosqlprotocol::host);
    add_native(&Configuration::authentication_required, &nosqlprotocol::authentication_required);
    add_native(&Configuration::authentication_shared, &nosqlprotocol::authentication_shared);
    add_native(&Configuration::authentication_db, &nosqlprotocol::authentication_db);
    add_native(&Configuration::authentication_key_id, &nosqlprotocol::authentication_key_id);
    add_native(&Configuration::authentication_user, &nosqlprotocol::authentication_user);
    add_native(&Configuration::authentication_password, &nosqlprotocol::authentication_password);
    add_native(&Configuration::authorization_enabled, &nosqlprotocol::authorization_enabled);
    add_native(&Configuration::id_length, &nosqlprotocol::id_length);
    add_native(&Configuration::pInternal_cache, &nosqlprotocol::internal_cache);

    add_native(&Configuration::auto_create_databases, &nosqlprotocol::auto_create_databases);
    add_native(&Configuration::auto_create_tables, &nosqlprotocol::auto_create_tables);
    add_native(&Configuration::cursor_timeout, &nosqlprotocol::cursor_timeout);
    add_native(&Configuration::debug, &nosqlprotocol::debug);
    add_native(&Configuration::log_unknown_command, &nosqlprotocol::log_unknown_command);
    add_native(&Configuration::on_unknown_command, &nosqlprotocol::on_unknown_command);
    add_native(&Configuration::ordered_insert_behavior, &nosqlprotocol::ordered_insert_behavior);
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
        rv = m_instance.post_configure(nested_params);
    }

    return rv;
}

// static
mxs::config::Specification& Configuration::specification()
{
    return nosqlprotocol::specification;
}

namespace
{

template<class Type>
bool get_optional(const string& command,
                  const bsoncxx::document::view& doc,
                  const string& key,
                  Type* pElement)
{
    bool rv = false;

    auto element = doc[key];

    if (element)
    {
        *pElement = nosql::element_as<Type>(command, key.c_str(), element);
        rv = true;
    }

    return rv;
}

}

namespace nosql
{

void Config::copy_from(const string& command, const bsoncxx::document::view& doc)
{
    Config that(*this);

    get_optional(command, doc, nosqlprotocol::auto_create_databases.name(), &that.auto_create_databases);
    get_optional(command, doc, nosqlprotocol::auto_create_tables.name(), &that.auto_create_tables);

    string s;

    if (get_optional(command, doc, nosqlprotocol::cursor_timeout.name(), &s))
    {
        string message;
        if (!nosqlprotocol::cursor_timeout.from_string(s, &that.cursor_timeout, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, nosqlprotocol::debug.name(), &s))
    {
        string message;
        if (!nosqlprotocol::debug.from_string(s, &that.debug, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, nosqlprotocol::log_unknown_command.name(), &s))
    {
        string message;
        if (!nosqlprotocol::log_unknown_command.from_string(s, &that.log_unknown_command, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, nosqlprotocol::on_unknown_command.name(), &s))
    {
        string message;
        if (!nosqlprotocol::on_unknown_command.from_string(s, &that.on_unknown_command, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, nosqlprotocol::ordered_insert_behavior.name(), &s))
    {
        string message;
        if (!nosqlprotocol::ordered_insert_behavior.from_string(s, &that.ordered_insert_behavior, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    const auto& specification = nosqlprotocol::specification;

    for (const auto& element : doc)
    {
        auto key = static_cast<string>(element.key());

        if (key == nosqlprotocol::user.name()
            || key == nosqlprotocol::password.name()
            || key == nosqlprotocol::id_length.name())
        {
            ostringstream ss;
            ss << "Configuration parameter '" << key << "', can only be changed via MaxScale.";
            throw SoftError(ss.str(), error::NO_SUCH_KEY);
        }

        if (!specification.find_param(static_cast<string>(element.key())))
        {
            ostringstream ss;
            ss << "Unknown configuration key: '" << element.key() << "'";
            throw SoftError(ss.str(), error::NO_SUCH_KEY);
        }
    }

    copy_from(that);
}

void Config::copy_to(nosql::DocumentBuilder& doc) const
{
    doc.append(kvp(nosqlprotocol::auto_create_databases.name(),
                   auto_create_databases));
    doc.append(kvp(nosqlprotocol::auto_create_tables.name(),
                   auto_create_tables));
    doc.append(kvp(nosqlprotocol::cursor_timeout.name(),
                   nosqlprotocol::cursor_timeout.to_string(cursor_timeout)));
    doc.append(kvp(nosqlprotocol::debug.name(),
                   nosqlprotocol::debug.to_string(debug)));
    doc.append(kvp(nosqlprotocol::log_unknown_command.name(),
                   nosqlprotocol::log_unknown_command.to_string(log_unknown_command)));
    doc.append(kvp(nosqlprotocol::on_unknown_command.name(),
                   nosqlprotocol::on_unknown_command.to_string(on_unknown_command)));
    doc.append(kvp(nosqlprotocol::ordered_insert_behavior.name(),
                   nosqlprotocol::ordered_insert_behavior.to_string(ordered_insert_behavior)));
}

}
