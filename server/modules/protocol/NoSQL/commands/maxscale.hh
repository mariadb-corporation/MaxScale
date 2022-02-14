/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "defs.hh"
#include "../nosqlscram.hh"
#include "../nosqlusermanager.hh"

namespace nosql
{

namespace command
{

class MxsAddUser final : public UserAdminAuthorize<ImmediateCommand>
{
public:
    static constexpr const char* const KEY = "mxsAddUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<ImmediateCommand>;
    using Base::Base;

    void populate_response(DocumentBuilder& doc) override
    {
        auto& um = m_database.context().um();

        string db = m_database.name();
        string user = value_as<string>();
        string pwd;
        string custom_data;
        vector<scram::Mechanism> mechanisms;
        vector<role::Role> roles;

        parse(KEY, um, m_doc, db, user, &pwd, &custom_data, &mechanisms, &roles);

        string host = m_database.config().host;

        if (um.add_user(db, user, pwd, host, custom_data, mechanisms, roles))
        {
            doc.append(kvp("ok", 1));
        }
        else
        {
            ostringstream ss;
            ss << "Could not add user " << user << "@" << db << " to the local nosqlprotocol "
               << "database. See maxscale.log for details.";

            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }
    }

    static void parse(const string& command,
                      const UserManager& um,
                      const bsoncxx::document::view& doc,
                      const string& db,
                      const string& user,
                      string* pPwd,
                      string* pCustom_data,
                      vector<scram::Mechanism>* pMechanisms,
                      vector<role::Role>* pRoles)
    {
        bool digest_password = true;
        if (nosql::optional(command, doc, key::DIGEST_PASSWORD, &digest_password) && !digest_password)
        {
            // Basically either the client or the server can digest the password.
            // If the client digested the password, then we could use the digested
            // password as the MariaDB password, which would mean that the actual
            // NoSQL password would not be stored on the MaxScale host. However,
            // since the MariaDB password really is the important one, it would not
            // add much value. Furthermore, a client digested password is not
            // supported with SCRAM-SHA-256, which is the default mechanism (not
            // supported yet), so we just won't bother.
            ostringstream ss;
            ss << "nosqlprotocol does not support that the client digests the password, "
               << "'digestPassword' must be true.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        bsoncxx::document::element element;

        element = doc[key::PWD];
        if (!element)
        {
            ostringstream ss;
            ss << "Must provide a '" << key::PWD << "' field for all user documents";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto type = element.type();
        if (type != bsoncxx::type::k_utf8)
        {
            ostringstream ss;
            ss << "\"" << key::PWD << "\" has the wrong type. Expected string, found "
               << bsoncxx::to_string(type);

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }

        string_view pwd = element.get_utf8();

        string custom_data;
        bsoncxx::document::view custom_data_doc;
        if (nosql::optional(command, doc, key::CUSTOM_DATA, &custom_data))
        {
            custom_data = bsoncxx::to_json(custom_data_doc);
        }

        vector<scram::Mechanism> mechanisms;
        element = doc[key::MECHANISMS];
        if (element && element.type() != bsoncxx::type::k_null)
        {
            if (element.type() != bsoncxx::type::k_array)
            {
                throw SoftError("mechanisms field must be an array", error::UNSUPPORTED_FORMAT);
            }

            bsoncxx::array::view array = element.get_array();

            if (array.empty())
            {
                throw SoftError("mechanisms field must not be empty", error::UNSUPPORTED_FORMAT);
            }

            scram::from_bson(array, &mechanisms); // Throws if invalid.
        }
        else
        {
            mechanisms = scram::supported_mechanisms();
        }

        element = doc[key::ROLES];
        if (!element || (element.type() != bsoncxx::type::k_array))
        {
            ostringstream ss;
            ss << "\"" << command << "\" command requires a \"" << key::ROLES << "\" array";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        vector<role::Role> roles;
        role::from_bson(element.get_array(), db, &roles);

        if (um.user_exists(db, user))
        {
            ostringstream ss;
            ss << "User \"" << user << "@" << db << "\" already exists";

            throw SoftError(ss.str(), error::LOCATION51003);
        }

        *pPwd = string(pwd.data(), pwd.length());
        *pCustom_data = std::move(custom_data);
        *pMechanisms = std::move(mechanisms);
        *pRoles = std::move(roles);
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

class MxsRemoveUser final : public UserAdminAuthorize<ImmediateCommand>
{
public:
    static constexpr const char* const KEY = "mxsRemoveUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<ImmediateCommand>;
    using Base::Base;

    void populate_response(DocumentBuilder& doc) override
    {
        auto& um = m_database.context().um();

        string db = m_database.name();
        string user = value_as<string>();

        if (!um.user_exists(db, user))
        {
            ostringstream ss;
            ss << "User '" << user << "@" << db << "' not found";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }

        if (!um.remove_user(db, user))
        {
            ostringstream ss;
            ss << "Could not remove user '" << user << "@" << db << "' not found";

            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }

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

class MxsUpdateUser final : public UserAdminAuthorize<ImmediateCommand>
{
public:
    static constexpr const char* const KEY = "mxsUpdateUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<ImmediateCommand>;
    using Base::Base;

    using UserInfo = nosql::UserManager::UserInfo;
    using Update = nosql::UserManager::Update;

    void populate_response(DocumentBuilder& doc) override
    {
        auto& um = m_database.context().um();
        string db = m_database.name();
        string user = value_as<string>();

        Update data;
        uint32_t what = parse(KEY, um, m_doc, db, user, &data);

        if (um.update(db, user, what, data))
        {
            doc.append(kvp(key::OK, 1));
        }
        else
        {
            ostringstream ss;
            ss << "Could not update the user " << user << "@" << db << ".";

            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }
    }

    static uint32_t parse(const string& command,
                          const UserManager& um,
                          const bsoncxx::document::view& doc,
                          const string& db,
                          const string& user,
                          Update* pData)
    {
        uint32_t what = 0;

        bool digest_password = true;
        if (nosql::optional(command, doc, key::DIGEST_PASSWORD, &digest_password) && !digest_password)
        {
            ostringstream ss;
            ss << "nosqlprotocol does not support that the client digests the password, "
               << "'digestPassword' must be true.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        UserInfo info;
        if (!um.get_info(db, user, &info))
        {
            ostringstream ss;
            ss << "Could not find user \"" << user << "\" for db \"" << db << "\"";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }

        Update data;
        if (nosql::optional(command, doc, key::PWD, &data.pwd))
        {
            what |= Update::PWD;
        }

        bsoncxx::document::view custom_data_doc;
        if (nosql::optional(command, doc, key::CUSTOM_DATA, &custom_data_doc))
        {
            data.custom_data = bsoncxx::to_json(custom_data_doc);
            what |= Update::CUSTOM_DATA;
        }

        bsoncxx::array::view mechanism_names;
        if (nosql::optional(command, doc, key::MECHANISMS, &mechanism_names))
        {
            scram::from_bson(mechanism_names, &data.mechanisms);

            if (!(what & Update::PWD))
            {
                // Password is not changed => new mechanisms must be subset of old.
                for (const auto mechanism : data.mechanisms)
                {
                    auto begin = info.mechanisms.begin();
                    auto end = info.mechanisms.end();

                    if (std::find(begin, end, mechanism) == end)
                    {
                        ostringstream ss;
                        ss << "mechanisms field must be a subset of previously set mechanisms";

                        throw SoftError(ss.str(), error::BAD_VALUE);
                    }
                }
            }

            what |= Update::MECHANISMS;
        }

        bsoncxx::array::view role_names;
        if (nosql::optional(command, doc, key::ROLES, &role_names))
        {
            role::from_bson(role_names, db, &data.roles);

            what |= Update::ROLES;
        }

        if (what == 0)
        {
            throw SoftError("Must specify at least one field to update in mxsUpdateUser", error::BAD_VALUE);
        }

        if ((what & Update::PWD) && !(what & Update::MECHANISMS))
        {
            // If the password is changed, but the mechanisms are not explicitly
            // specified, we use the current mechanisms.
            data.mechanisms = info.mechanisms;
        }

        *pData = std::move(data);

        return what;
    }
};

}

}
