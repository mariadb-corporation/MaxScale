/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlconfig.hh"
#include "protocolmodule.hh"

using namespace std;

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
    using C = Configuration;

    Config that(*this);

    get_optional(command, doc, C::s_auto_create_databases.name(), &that.auto_create_databases);
    get_optional(command, doc, C::s_auto_create_tables.name(), &that.auto_create_tables);

    string s;

    if (get_optional(command, doc, C::s_cursor_timeout.name(), &s))
    {
        string message;
        if (!C::s_cursor_timeout.from_string(s, &that.cursor_timeout, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, C::s_debug.name(), &s))
    {
        string message;
        if (!C::s_debug.from_string(s, &that.debug, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, C::s_log_unknown_command.name(), &s))
    {
        string message;
        if (!C::s_log_unknown_command.from_string(s, &that.log_unknown_command, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, C::s_on_unknown_command.name(), &s))
    {
        string message;
        if (!C::s_on_unknown_command.from_string(s, &that.on_unknown_command, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    if (get_optional(command, doc, C::s_ordered_insert_behavior.name(), &s))
    {
        string message;
        if (!C::s_ordered_insert_behavior.from_string(s, &that.ordered_insert_behavior, &message))
        {
            throw SoftError(message, error::BAD_VALUE);
        }
    }

    const auto& specification = C::specification();

    for (const auto& element : doc)
    {
        auto key = static_cast<string>(element.key());

        if (key == C::s_user.name() || key == C::s_password.name() || key == C::s_id_length.name())
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
    using C = Configuration;

    doc.append(kvp(C::s_auto_create_databases.name(),
                   auto_create_databases));
    doc.append(kvp(C::s_auto_create_tables.name(),
                   auto_create_tables));
    doc.append(kvp(C::s_cursor_timeout.name(),
                   C::s_cursor_timeout.to_string(cursor_timeout)));
    doc.append(kvp(C::s_debug.name(),
                   C::s_debug.to_string(debug)));
    doc.append(kvp(C::s_log_unknown_command.name(),
                   C::s_log_unknown_command.to_string(log_unknown_command)));
    doc.append(kvp(C::s_on_unknown_command.name(),
                   C::s_on_unknown_command.to_string(on_unknown_command)));
    doc.append(kvp(C::s_ordered_insert_behavior.name(),
                   C::s_ordered_insert_behavior.to_string(ordered_insert_behavior)));
}

}
