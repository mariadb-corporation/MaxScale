/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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
            DocumentArguments arguments;
            unique_ptr<OpMsgCommand> sCommand;

            packet::Msg req(m_req);
            sCommand = OpMsgCommand::get(&m_database, m_pRequest, std::move(req), command, arguments);

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
        DocumentBuilder config;
        c.copy_to(config);

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
        auto& config = m_database.config();

        config.copy_from(KEY, value_as<bsoncxx::document::view>());

        MxsGetConfig::populate_response(doc, config);
    }
};

class MxsCreateDatabase;

template<>
struct IsAdmin<MxsCreateDatabase>
{
    static const bool is_admin { true };
};

class MxsCreateDatabase final : public SingleCommand
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

        return State::READY;
    }

private:
    string m_name;
};

}

}
