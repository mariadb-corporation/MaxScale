/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "defs.hh"

namespace nosql
{

namespace command
{

class MxsDiagnose final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "mxsDiagnose";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        auto command = value_as<bsoncxx::document::view>();

        if (!command.empty())
        {
            string name = static_cast<string>(command.begin()->key());

            DocumentArguments arguments;
            unique_ptr<Command> sCommand;

            if (m_req.opcode() == MONGOC_OPCODE_QUERY)
            {
                Query& query = static_cast<Query&>(m_req);

                sCommand = Command::get(&m_database, m_pRequest, query, command, arguments);
            }
            else
            {
                Msg& msg = static_cast<Msg&>(m_req);

                sCommand = Command::get(&m_database, m_pRequest, msg, command, arguments);
            }

            try
            {
                sCommand->diagnose(doc);
            }
            catch (const Exception& x)
            {
                doc.clear();

                DocumentBuilder error;
                x.create_response(*sCommand, error);

                doc.append(kvp(key::ERROR, error.extract()));
            }
            catch (const std::exception& x)
            {
                doc.clear();

                doc.append(kvp(key::ERROR, x.what()));
            }
        }

        doc.append(kvp(key::OK, 1));
    }
};


class MxsGetConfig;

template<>
struct IsAdmin<MxsGetConfig>
{
    static const bool is_admin { true };
};

class MxsGetConfig final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "mxsGetConfig";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    bool is_admin() const override
    {
        return IsAdmin<MxsGetConfig>::is_admin;
    }

    void populate_response(DocumentBuilder& doc) override
    {
        populate_response(doc, m_database.config());
    }

    static void populate_response(DocumentBuilder& doc, const Config& c)
    {
        using C = GlobalConfig;

        DocumentBuilder config;
        config.append(kvp(C::s_on_unknown_command.name(),
                          C::s_on_unknown_command.to_string(c.on_unknown_command)));
        config.append(kvp(C::s_auto_create_databases.name(), c.auto_create_databases));
        config.append(kvp(C::s_auto_create_tables.name(), c.auto_create_tables));
        config.append(kvp(C::s_id_length.name(), static_cast<int32_t>(c.id_length)));
        config.append(kvp(C::s_ordered_insert_behavior.name(),
                          C::s_ordered_insert_behavior.to_string(c.ordered_insert_behavior)));

        doc.append(kvp(key::CONFIG, config.extract()));
        doc.append(kvp(key::OK, 1));
    }
};


class MxsSetConfig;

template<>
struct IsAdmin<MxsSetConfig>
{
    static const bool is_admin { true };
};

class MxsSetConfig final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "mxsSetConfig";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    bool is_admin() const override
    {
        return IsAdmin<MxsSetConfig>::is_admin;
    }

    void populate_response(DocumentBuilder& doc) override
    {
        using C = GlobalConfig;
        auto& c = m_database.config();

        auto on_unknown_command = c.on_unknown_command;
        auto auto_create_databases = c.auto_create_databases;
        auto auto_create_tables = c.auto_create_tables;
        auto id_length = c.id_length;
        auto ordered_insert_behavior = c.ordered_insert_behavior;

        const auto& config = value_as<bsoncxx::document::view>();

        string s;
        if (optional(config, C::s_on_unknown_command.name(), &s))
        {
            string message;
            if (!C::s_on_unknown_command.from_string(s, &on_unknown_command, &message))
            {
                throw SoftError(message, error::BAD_VALUE);
            }
        }

        optional(config, C::s_auto_create_databases.name(), &auto_create_databases);
        optional(config, C::s_auto_create_tables.name(), &auto_create_tables);

        if (optional(config, C::s_id_length.name(), &id_length, Conversion::RELAXED))
        {
            // TODO: Ass-backwards that we must turn it into a string before we can
            // TODO: check whether it is valid *and* get a message descrbing the problem.

            string message;
            if (!C::s_id_length.from_string(std::to_string(id_length), &id_length, &message))
            {
                throw SoftError(message, error::BAD_VALUE);
            }
        }

        if (optional(config, C::s_ordered_insert_behavior.name(), &s))
        {
            string message;
            if (!C::s_ordered_insert_behavior.from_string(s, &ordered_insert_behavior, &message))
            {
                throw SoftError(message, error::BAD_VALUE);
            }
        }

        const auto& specification = C::specification();

        for (const auto& element : config)
        {
            if (!specification.find_param(static_cast<string>(element.key())))
            {
                ostringstream ss;
                ss << "Unknown configuration key: '" << element.key() << "'";
                throw SoftError(ss.str(), error::NO_SUCH_KEY);
            }
        }

        c.on_unknown_command = on_unknown_command;
        c.auto_create_databases = auto_create_databases;
        c.auto_create_tables = auto_create_tables;
        c.id_length = id_length;
        c.ordered_insert_behavior = ordered_insert_behavior;

        MxsGetConfig::populate_response(doc, c);
    }
};

class MxsCreateDatabase;

template<>
struct IsAdmin<MxsCreateDatabase>
{
    static const bool is_admin { true };
};

class MxsCreateDatabase : public SingleCommand
{
public:
    static constexpr const char* const KEY = "mxsCreateDatabase";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    bool is_admin() const override
    {
        return IsAdmin<MxsCreateDatabase>::is_admin;
    }

    string generate_sql() override
    {
        m_name = value_as<string>();

        ostringstream sql;
        sql << "CREATE DATABASE `" << m_name << "`";

        return sql.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

        DocumentBuilder doc;

        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_DB_CREATE_EXISTS)
                {
                    ostringstream ss;
                    ss << "The database '" << m_name << "' exists already.";

                    throw SoftError(ss.str(), error::NAMESPACE_EXISTS);
                }
                else
                {
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        doc.append(kvp(key::OK, ok));

        *ppResponse = create_response(doc.extract());

        return READY;
    }

private:
    string m_name;
};

}

}
