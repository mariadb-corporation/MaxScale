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
#pragma once

//
// https://docs.mongodb.com/v4.4/reference/command/nav-user-management/
//
#include "defs.hh"
#include <uuid/uuid.h>
#include "../nosqlscram.hh"
#include "../nosqlusermanager.hh"
#include "maxscale.hh"

using namespace std;

namespace nosql
{

namespace
{

using UserInfo = nosql::UserManager::UserInfo;

vector<string> create_grant_or_revoke_statements(const string& user,
                                                 const string& command,
                                                 const string& preposition,
                                                 const vector<role::Role>& roles)
{
    vector<string> statements;

    for (const auto& role : roles)
    {
        bool is_on_admin = (role.db == "admin");

        string db = role.db;

        vector<string> privileges;

        switch (role.id)
        {
        case role::Id::DB_ADMIN_ANY_DATABASE:
            if (is_on_admin)
            {
                db = "*";
            }
            else
            {
                ostringstream ss;
                ss << "No role names dbAdminAnyDatabase@" << role.db;
                throw SoftError(ss.str(), error::ROLE_NOT_FOUND);
            }
        case role::Id::DB_ADMIN:
            privileges.push_back("ALTER");
            privileges.push_back("CREATE");
            privileges.push_back("DROP");
            privileges.push_back("SHOW DATABASES");
            privileges.push_back("SELECT");
            break;

        case role::Id::READ_WRITE_ANY_DATABASE:
            if (is_on_admin)
            {
                db = "*";
            }
            else
            {
                ostringstream ss;
                ss << "No role names readWriteAnyDatabase@" << role.db;
                throw SoftError(ss.str(), error::ROLE_NOT_FOUND);
            }
        case role::Id::READ_WRITE:
            privileges.push_back("CREATE");
            privileges.push_back("DELETE");
            privileges.push_back("INDEX");
            privileges.push_back("INSERT");
            privileges.push_back("SELECT");
            privileges.push_back("UPDATE");
            break;

        case role::Id::READ_ANY_DATABASE:
            if (is_on_admin)
            {
                db = "*";
            }
            else
            {
                ostringstream ss;
                ss << "No role names readAnyDatabase@" << role.db;
                throw SoftError(ss.str(), error::ROLE_NOT_FOUND);
            }
        case role::Id::READ:
            privileges.push_back("SELECT");
            break;

        case role::Id::ROOT:
            {
                if (is_on_admin)
                {
                    db = "*";
                }

                // CREATE USER is global, so must be applied to *.*. Easiest is just
                // use a specific statement.
                string statement = command + "CREATE USER ON *.*" + preposition + user;
                statements.push_back(statement);

                privileges.push_back("CREATE");
                privileges.push_back("DELETE");
                privileges.push_back("INDEX");
                privileges.push_back("INSERT");
                privileges.push_back("SELECT");
                privileges.push_back("UPDATE");
            }
            break;

        case role::Id::USER_ADMIN:
            {
                if (is_on_admin)
                {
                    db = "*";
                }

                // CREATE USER is global, so must be applied to *.*. Easiest is just
                // use a specific statement.
                string statement = command + "CREATE USER ON *.*" + preposition + user;
                statements.push_back(statement);

                privileges.push_back("GRANT OPTION");
            }
            break;

        default:
            MXS_WARNING("Role %s granted/revoked to/from %s is ignored.",
                        role::to_string(role.id).c_str(), user.c_str());
        }

        string statement = command + mxb::join(privileges) + " ON " + db + ".*" + preposition + user;

        statements.push_back(statement);
    }

    return statements;
}

vector<string> create_grant_statements(const string& user, const vector<role::Role>& roles)
{
    return create_grant_or_revoke_statements(user, "GRANT ", " TO ", roles);
}

vector<string> create_revoke_statements(const string& user,const vector<role::Role>& roles)
{
    return create_grant_or_revoke_statements(user, "REVOKE ", " FROM ", roles);
}

string get_nosql_account(const string& db, const string& user)
{
    return user + "@" + db;
}

}

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/createUser/
class CreateUser final : public UserAdminAuthorize<SingleCommand>
{
public:
    static constexpr const char* const KEY = "createUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<SingleCommand>;
    using Base::Base;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        State state = State::READY;

        switch (m_action)
        {
        case Action::CREATE:
            state = translate_create(std::move(mariadb_response), ppNoSQL_response);
            break;

        case Action::DROP:
            state = translate_drop(std::move(mariadb_response), ppNoSQL_response);
            break;
        }

        return state;
    }

protected:
    void prepare() override
    {
        auto& um = m_database.context().um();

        m_db = m_database.name();
        m_user = value_as<string>();

        MxsAddUser::parse(KEY, um, m_doc, m_db, m_user, &m_pwd, &m_custom_data, &m_mechanisms, &m_roles);

        m_host = m_database.config().host;
    }

    string generate_sql() override
    {
        string account = mariadb::get_account(m_db, m_user, m_host);

        m_statements.push_back("CREATE USER " + account + " IDENTIFIED BY '" + m_pwd + "'");

        auto grants = create_grant_statements(account, m_roles);

        m_statements.insert(m_statements.end(), grants.begin(), grants.end());

        return mxb::join(m_statements, ";");
    }

private:
    void check_create(const ComResponse& response)
    {
        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_CANNOT_USER:
                    {
                        // We assume it's because the user exists.
                        ostringstream ss;
                        ss << "User \"" << m_user << "\" already exists";

                        throw SoftError(ss.str(), error::LOCATION51003);
                    }
                    break;

                case ER_SPECIFIC_ACCESS_DENIED_ERROR:
                    {
                        ostringstream ss;
                        ss << "not authorized on " << m_database.name() << " to execute command "
                           << bsoncxx::to_json(m_doc);

                        throw SoftError(ss.str(), error::UNAUTHORIZED);
                    }
                    break;

                default:
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }
    }

    bool check_grant(const ComResponse& response, int i)
    {
        bool success = true;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                MXS_ERROR("Could create user '%s.%s'@'%s', but granting access with the "
                          "statement \"%s\" failed with: (%d) \"%s\". Will now attempt to "
                          "DROP the user.",
                          m_db.c_str(),
                          m_user.c_str(),
                          m_host.c_str(),
                          m_statements[i].c_str(),
                          err.code(),
                          err.message().c_str());

                success = false;
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        return success;
    }

    State translate_create(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
    {
        State state = State::READY;

        uint8_t* pData = mariadb_response.data();
        uint8_t* pEnd = pData + mariadb_response.length();

        size_t i = 0;
        DocumentBuilder doc;

        bool success = true;
        while ((pData < pEnd) && success)
        {
            ComResponse response(&pData);

            if (i == 0)
            {
                check_create(response);
            }
            else
            {
                success = check_grant(response, i);
            }

            ++i;
        }

        if (success)
        {
            mxb_assert(i == m_statements.size());

            const auto& config = m_database.config();
            auto& um = m_database.context().um();

            if (um.add_user(m_db, m_user, m_pwd, config.host, m_custom_data, m_mechanisms, m_roles))
            {
                doc.append(kvp("ok", 1));
            }
            else
            {
                ostringstream ss;
                ss << "Could add user '" << m_user << "' to the MariaDB database, "
                   << "but could not add the user to the local database " << um.path() << ".";

                string message = ss.str();

                MXS_ERROR("%s", message.c_str());

                throw SoftError(message, error::INTERNAL_ERROR);
            }

            *ppNoSQL_response = create_response(doc.extract());
            state = State::READY;
        }
        else
        {
            // Ok, so GRANTing access failed. To make everything simpler for everyone, will
            // now attempt to DROP the user.

            state = State::BUSY;

            m_action = Action::DROP;

            ostringstream sql;
            sql << "DROP USER '" << m_db << "." << m_user << "'@'" << m_host << "'";

            send_downstream_via_loop(sql.str());
        }

        return state;
    }

    State translate_drop(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
    {
        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            {
                ostringstream ss;
                ss << "Could create MariaDB user '" << m_db << "." << m_user << "'@'" << m_host << "', "
                   << "but could not give the required GRANTs. The current used does not have "
                   << "the required privileges. See the MaxScale log for more details.";

                throw SoftError(ss.str(), error::UNAUTHORIZED);
            }
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                ostringstream ss;
                ss << "Could create MariaDB user '" << m_db << "." << m_user << "'@'" << m_host << "', "
                   << "but could not give the required GRANTs and the subsequent attempt to delete "
                   << "the user failed: (" << err.code() << ") \"" << err.message() << "\". "
                   << "You should now DROP the user manually.";

                throw SoftError(ss.str(), error::INTERNAL_ERROR);
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        mxb_assert(!true);
        return State::READY;
    }

private:
    enum class Action
    {
        CREATE,
        DROP
    };

    Action                   m_action = Action::CREATE;
    string                   m_db;
    string                   m_user;
    string                   m_pwd;
    string                   m_host;
    std::string              m_custom_data;
    vector<scram::Mechanism> m_mechanisms;
    vector<role::Role>       m_roles;
    vector<string>           m_statements;
    uint32_t                 m_dcid = { 0 };
};

// https://docs.mongodb.com/v4.4/reference/command/dropAllUsersFromDatabase/
class DropAllUsersFromDatabase final : public UserAdminAuthorize<SingleCommand>
{
public:
    static constexpr const char* const KEY = "dropAllUsersFromDatabase";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<SingleCommand>;
    using Base::Base;

    State execute(GWBUF** ppNoSQL_response) override final
    {
        State state = State::READY;

        const auto& um = m_database.context().um();

        m_mariadb_accounts = um.get_mariadb_accounts(m_database.name());

        if (m_mariadb_accounts.empty())
        {
            DocumentBuilder doc;
            long n = 0;
            doc.append(kvp(key::N, n));
            doc.append(kvp(key::OK, 1));

            *ppNoSQL_response = create_response(doc.extract());
        }
        else
        {
            state = SingleCommand::execute(ppNoSQL_response);
        }

        return state;
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        State state = State::READY;

        uint8_t* pData = mariadb_response.data();
        uint8_t* pEnd = pData + mariadb_response.length();

        long n = 0;
        while (pData < pEnd)
        {
            ComResponse response(&pData);

            switch (response.type())
            {
            case ComResponse::OK_PACKET:
                ++n;
                break;

            case ComResponse::ERR_PACKET:
                {
                    ComERR err(response);

                    switch (err.code())
                    {
                    case ER_SPECIFIC_ACCESS_DENIED_ERROR:
                        if (n == 0)
                        {
                            ostringstream ss;
                            ss << "not authorized on " << m_database.name() << " to execute command "
                               << bsoncxx::to_json(m_doc).c_str();

                            throw SoftError(ss.str(), error::UNAUTHORIZED);
                        }
                        else
                        {
                            vector<string> users;
                            for (int i = 0; i < n; ++i)
                            {
                                string mariadb_user = "'" + m_mariadb_accounts[i].user + "'";
                                users.push_back(mariadb_user);
                            }

                            MXS_WARNING("Dropping users %s succeeded, but dropping '%s' failed: %s",
                                        mxb::join(users, ",").c_str(),
                                        m_mariadb_accounts[n].user.c_str(),
                                        err.message().c_str());
                        }
                        break;

                    case ER_CANNOT_USER:
                        MXS_WARNING("User '%s' apparently did not exist in the MariaDB server, even "
                                    "though it should according to the nosqlprotocol book-keeping.",
                                    m_mariadb_accounts[n].user.c_str());
                        break;

                    default:
                        MXS_ERROR("Dropping user '%s' failed: %s",
                                  m_mariadb_accounts[n].user.c_str(),
                                  err.message().c_str());
                    };
                };
            }
        }

        mxb_assert(pData == pEnd);

        vector<UserManager::MariaDBAccount> accounts = m_mariadb_accounts;
        accounts.resize(n);

        const auto& um = m_database.context().um();

        if (!um.remove_mariadb_accounts(accounts))
        {
            ostringstream ss;
            ss << "Could remove " << n << " users from MariaDB, but could not remove "
               << "users from the local nosqlprotocol database. The user information "
               << "may now be out of sync.";

            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }

        DocumentBuilder doc;
        doc.append(kvp(key::N, n));
        doc.append(kvp(key::OK, 1));

        *ppNoSQL_response = create_response(doc.extract());
        return State::READY;
    }

protected:
    string generate_sql() override final
    {
        mxb_assert(!m_mariadb_accounts.empty());

        vector<string> statements;
        for (const auto& mariadb_account : m_mariadb_accounts)
        {
            statements.push_back("DROP USER '" + mariadb_account.user + "'@'" + mariadb_account.host + "'");
        }

        return mxb::join(statements, ";");
    };

private:
    vector<UserManager::MariaDBAccount> m_mariadb_accounts;
};

// https://docs.mongodb.com/v4.4/reference/command/dropUser/
class DropUser final : public UserAdminAuthorize<SingleCommand>
{
public:
    static constexpr const char* const KEY = "dropUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<SingleCommand>;
    using Base::Base;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        ComResponse response(mariadb_response.data());

        DocumentBuilder doc;

        switch (response.type())
        {
        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_CANNOT_USER:
                    {
                        // We assume it's because the user does not exist.
                        ostringstream ss;
                        ss << "User \"" << get_nosql_account(m_db, m_user) << "\" not found";

                        throw SoftError(ss.str(), error::USER_NOT_FOUND);
                    }
                    break;

                case ER_SPECIFIC_ACCESS_DENIED_ERROR:
                    {
                        ostringstream ss;
                        ss << "not authorized on " << m_database.name() << " to execute command "
                           << bsoncxx::to_json(m_doc);

                        throw SoftError(ss.str(), error::UNAUTHORIZED);
                    }
                    break;

                default:
                    throw MariaDBError(err);
                }
            }
            break;

        case ComResponse::OK_PACKET:
            {
                auto& um = m_database.context().um();

                if (um.remove_user(m_db, m_user))
                {
                    doc.append(kvp("ok", 1));
                }
                else
                {
                    ostringstream ss;
                    ss << "Could remove user \"" << get_nosql_account(m_db, m_user) << "\" from "
                       << "MariaDB backend, but not from local database.";

                    throw SoftError(ss.str(), error::INTERNAL_ERROR);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppNoSQL_response = create_response(doc.extract());
        return State::READY;
    }

protected:
    void prepare() override
    {
        m_db = m_database.name();
        m_user = value_as<string>();

        auto& um = m_database.context().um();

        UserManager::MariaDBAccount mariadb_account;
        if (!um.get_mariadb_account(m_db, m_user, &mariadb_account))
        {
            ostringstream ss;
            ss << "User \"" << get_nosql_account(m_db, m_user) << "\" not found";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }

        m_host = mariadb_account.host;
    }

    string generate_sql() override
    {
        ostringstream sql;

        sql << "DROP USER '" << m_db << "." << m_user << "'@'" << m_host << "'";

        return sql.str();
    }

private:
    string m_db;
    string m_user;
    string m_host;
};

// https://docs.mongodb.com/v4.4/reference/command/grantRolesToUser/
class GrantRolesToUser : public UserAdminAuthorize<SingleCommand>
{
public:
    static constexpr const char* const KEY = "grantRolesToUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<SingleCommand>;
    using Base::Base;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        uint8_t* pData = mariadb_response.data();
        uint8_t* pEnd = pData + mariadb_response.length();

        size_t n = 0;
        while (pData < pEnd)
        {
            ComResponse response(&pData);

            switch (response.type())
            {
            case ComResponse::OK_PACKET:
                ++n;
                break;

            case ComResponse::ERR_PACKET:
                {
                    ComERR err(response);

                    switch (err.code())
                    {
                    case ER_SPECIFIC_ACCESS_DENIED_ERROR:
                        if (n == 0)
                        {
                            ostringstream ss;
                            ss << "not authorized on " << m_database.name() << " to execute command "
                               << bsoncxx::to_json(m_doc).c_str();

                            throw SoftError(ss.str(), error::UNAUTHORIZED);
                        }
                        // fallthrough
                    default:
                        MXS_ERROR("Grant statement '%s' failed: %s",
                                  m_statements[n].c_str(), err.message().c_str());
                    }
                };
                break;

            default:
                throw_unexpected_packet();
            }
        }

        auto granted_roles = m_roles;
        granted_roles.resize(n);

        map<string, set<role::Id>> roles_by_db;

        for (const auto& role : m_info.roles)
        {
            roles_by_db[role.db].insert(role.id);
        }

        for (const auto& role : granted_roles)
        {
            roles_by_db[role.db].insert(role.id);
        }

        vector<role::Role> final_roles;

        for (const auto& kv : roles_by_db)
        {
            const auto& db = kv.first;

            for (const auto& id : kv.second)
            {
                role::Role role { db, id };

                final_roles.push_back(role);
            }
        }

        const auto& um = m_database.context().um();

        UserManager::UserInfo info;
        info.roles = final_roles;

        if (um.update(m_db, m_user, UserInfo::ROLES, info))
        {
            if (n == m_roles.size())
            {
                DocumentBuilder doc;
                doc.append(kvp(key::OK, 1));

                *ppNoSQL_response = create_response(doc.extract());
            }
            else
            {
                ostringstream ss;

                ss << "Could partially update the MariaDB grants and could update the corresponding "
                   << "roles in the local nosqlprotocol database. See the MaxScale log for more details.";

                throw SoftError(ss.str(), error::INTERNAL_ERROR);
            }
        }
        else
        {
            ostringstream ss;

            if (n == m_roles.size())
            {
                ss << "Could update the MariaDB grants";
            }
            else
            {
                ss << "Could partially update the MariaDB grants";
            }

            ss << ", but could not update the roles in the local nosqlprotocol database. "
               << "There is now a discrepancy between the grants the user has and the roles "
               << "nosqlprotocol think it has.";

            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }

        return State::READY;
    }

private:
    void prepare() override
    {
        m_db = m_database.name();
        m_user = value_as<string>();

        auto element = m_doc[key::ROLES];

        if (!element
            || (element.type() != bsoncxx::type::k_array)
            || (static_cast<bsoncxx::array::view>(element.get_array()).empty()))
        {
            ostringstream ss;
            ss << "\"grantRoles\" command requires a non-empty \"" << key::ROLES << "\" array";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        role::from_bson(element.get_array(), m_db, &m_roles);

        auto& um = m_database.context().um();

        if (!um.get_info(m_db, m_user, &m_info))
        {
            ostringstream ss;
            ss << "Could not find user \"" << m_user << " for db \"" << m_db << "\"";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }
    }

    string generate_sql() override
    {
        string account = mariadb::get_account(m_db, m_user, m_info.host);

        m_statements = create_grant_statements(account, m_roles);

        return mxb::join(m_statements, ";");
    }

private:
    string                m_db;
    string                m_user;
    UserManager::UserInfo m_info;
    vector<role::Role>    m_roles;
    vector<string>        m_statements;
};

// https://docs.mongodb.com/v4.4/reference/command/revokeRolesFromUser/
class RevokeRolesFromUser : public UserAdminAuthorize<SingleCommand>
{
public:
    static constexpr const char* const KEY = "revokeRolesFromUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<SingleCommand>;
    using Base::Base;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        uint8_t* pData = mariadb_response.data();
        uint8_t* pEnd = pData + mariadb_response.length();

        size_t n = 0;
        while (pData < pEnd)
        {
            ComResponse response(&pData);

            switch (response.type())
            {
            case ComResponse::OK_PACKET:
                ++n;
                break;

            case ComResponse::ERR_PACKET:
                {
                    ComERR err(response);

                    switch (err.code())
                    {
                    case ER_SPECIFIC_ACCESS_DENIED_ERROR:
                        if (n == 0)
                        {
                            ostringstream ss;
                            ss << "not authorized on " << m_database.name() << " to execute command "
                               << bsoncxx::to_json(m_doc).c_str();

                            throw SoftError(ss.str(), error::UNAUTHORIZED);
                        }
                        // fallthrough
                    default:
                        MXS_ERROR("Revoke statement '%s' failed: %s",
                                  m_statements[n].c_str(), err.message().c_str());
                    }
                };
                break;

            default:
                throw_unexpected_packet();
            }
        }

        auto revoked_roles = m_roles;
        revoked_roles.resize(n);

        map<string, set<role::Id>> roles_by_db;

        for (const auto& role : m_info.roles)
        {
            roles_by_db[role.db].insert(role.id);
        }

        for (const auto& role : revoked_roles)
        {
            set<role::Id>& role_ids = roles_by_db[role.db];

            role_ids.erase(role.id);
        }

        vector<role::Role> final_roles;

        for (const auto& kv : roles_by_db)
        {
            const auto& db = kv.first;

            if (!kv.second.empty())
            {
                for (const auto& id : kv.second)
                {
                    role::Role role { db, id };

                    final_roles.push_back(role);
                }
            }
        }

        const auto& um = m_database.context().um();

        if (um.set_roles(m_db, m_user, final_roles))
        {
            if (n == m_roles.size())
            {
                DocumentBuilder doc;
                doc.append(kvp(key::OK, 1));

                *ppNoSQL_response = create_response(doc.extract());
            }
            else
            {
                ostringstream ss;

                ss << "Could partially update the MariaDB grants and could update the corresponding "
                   << "roles in the local nosqlprotocol database. See the MaxScale log for more details.";

                throw SoftError(ss.str(), error::INTERNAL_ERROR);
            }
        }
        else
        {
            ostringstream ss;

            if (n == m_roles.size())
            {
                ss << "Could update the MariaDB grants";
            }
            else
            {
                ss << "Could partially update the MariaDB grants";
            }

            ss << ", but could not update the roles in the local nosqlprotocol database. "
               << "There is now a discrepancy between the grants the user has and the roles "
               << "nosqlprotocol think it has.";

            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }

        return State::READY;
    }

private:
    void prepare() override
    {
        m_db = m_database.name();
        m_user = value_as<string>();

        auto element = m_doc[key::ROLES];

        if (!element
            || (element.type() != bsoncxx::type::k_array)
            || (static_cast<bsoncxx::array::view>(element.get_array()).empty()))
        {
            ostringstream ss;
            ss << "\"revokeRoles\" command requires a non-empty \"" << key::ROLES << "\" array";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        role::from_bson(element.get_array(), m_db, &m_roles);

        auto& um = m_database.context().um();

        if (!um.get_info(m_db, m_user, &m_info))
        {
            ostringstream ss;
            ss << "Could not find user \"" << m_user << " for db \"" << m_db << "\"";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }
    }

    string generate_sql() override
    {
        string account = mariadb::get_account(m_db, m_user, m_info.host);

        m_statements = create_revoke_statements(account, m_roles);

        return mxb::join(m_statements, ";");
    }

private:
    string                m_db;
    string                m_user;
    UserManager::UserInfo m_info;
    vector<role::Role>    m_roles;
    vector<string>        m_statements;
};

// https://docs.mongodb.com/v4.4/reference/command/updateUser/
class UpdateUser : public UserAdminAuthorize<SingleCommand>
{
public:
    static constexpr const char* const KEY = "updateUser";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<SingleCommand>;
    using Base::Base;

    State execute(GWBUF** ppNoSQL_response) override
    {
        State state;

        m_db = m_database.name();
        m_user = value_as<string>();

        auto& um = m_database.context().um();

        if (!um.get_info(m_db, m_user, &m_old_info))
        {
            ostringstream ss;
            ss << "Could not find user \"" << m_user << "\" for db \"" << m_db << "\"";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }

        m_what = MxsUpdateUser::parse(KEY, um, m_doc, m_db, m_user, &m_new_info);

        if ((m_what & ~(UserInfo::CUSTOM_DATA | UserInfo::MECHANISMS)) != 0)
        {
            // Something else but the mechanisms and/or custom_data is updated.
            state = SingleCommand::execute(ppNoSQL_response);
        }
        else
        {
            if (um.update(m_db, m_user, m_what, m_new_info))
            {
                DocumentBuilder doc;
                doc.append(kvp(key::OK, 1));

                *ppNoSQL_response = create_response(doc.extract());
                state = State::READY;
            }
            else
            {
                throw SoftError("Could not update 'mechanisms' and/or 'custom_data'.", error::INTERNAL_ERROR);
            }
        }

        return state;
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        State state = State::READY;

        switch (m_action)
        {
        case Action::UPDATE_PASSWORD:
            state = translate_update_pwd(std::move(mariadb_response), ppNoSQL_response);
            break;

        case Action::UPDATE_GRANTS:
            state = translate_update_grants(std::move(mariadb_response), ppNoSQL_response);
            break;
        }

        return state;
    }

protected:
    string generate_sql() override
    {
        string sql;

        if (m_what & UserInfo::PWD)
        {
            sql = generate_update_pwd();
        }
        else if (m_what & UserInfo::ROLES)
        {
            sql = generate_update_grants();
        }
        else
        {
            mxb_assert(!true);
        }

        return sql;
    }

private:
    string generate_update_pwd()
    {
        m_action = Action::UPDATE_PASSWORD;

        m_statements.clear();

        string account = mariadb::get_account(m_db, m_user, m_old_info.host);

        mxb_assert(m_what & UserInfo::PWD);

        ostringstream ss;
        ss << "SET PASSWORD FOR " << account << " = PASSWORD('" << m_new_info.pwd << "')";

        string s = ss.str();

        m_statements.push_back(s);


        return s;
    }

    string generate_update_grants()
    {
        m_action = Action::UPDATE_GRANTS;

        m_statements.clear();

        string account = mariadb::get_account(m_db, m_user, m_old_info.host);

         // Revoke according to current roles.
        auto revokes = create_revoke_statements(account, m_old_info.roles);
        m_nRevokes = revokes.size();

        for (const auto& revoke : revokes)
        {
            m_statements.push_back(revoke);
        }

        // Grant according to new roles.
        auto grants = create_grant_statements(account, m_new_info.roles);
        m_nGrants = grants.size();

        for (const auto& grant : grants)
        {
            m_statements.push_back(grant);
        }

        return mxb::join(m_statements, ";");
    }

    State translate_update_pwd(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
    {
        State state = State::READY;

        uint8_t* pData = mariadb_response.data();

        ComResponse response(&pData);
        mxb_assert(pData == mariadb_response.data() + mariadb_response.length());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            {
                const auto& um = m_database.context().um();

                UserInfo info;
                info.pwd = m_new_info.pwd;
                uint32_t what = UserInfo::PWD;

                if (m_what & UserInfo::CUSTOM_DATA)
                {
                    info.custom_data = m_new_info.custom_data;
                    what |= UserInfo::CUSTOM_DATA;
                }

                if (m_what & UserInfo::MECHANISMS)
                {
                    info.mechanisms = m_new_info.mechanisms;
                    what |= UserInfo::MECHANISMS;
                }

                m_what &= ~(UserInfo::PWD | UserInfo::CUSTOM_DATA | UserInfo::MECHANISMS);

                if (um.update(m_db, m_user, what, info))
                {
                    if (m_what & UserInfo::ROLES)
                    {
                        auto sql = generate_update_grants();

                        send_downstream_via_loop(sql);
                        state = State::BUSY;
                    }
                    else
                    {
                        DocumentBuilder doc;
                        doc.append(kvp(key::OK, 1));

                        *ppNoSQL_response = create_response(doc.extract());
                        state = State::READY;
                    }
                }
                else
                {
                    ostringstream ss;
                    ss << "Could update the password in the MariaDB server, but could not store "
                       << "it in the local nosqlprotocol database. It will no longer be possible "
                       << "to log in as \"" << get_nosql_account(m_db, m_user) << "\".";

                    throw SoftError(ss.str(), error::INTERNAL_ERROR);
                }
            }
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_SPECIFIC_ACCESS_DENIED_ERROR:
                    {
                        ostringstream ss;
                        ss << "not authorized on " << m_database.name() << " to execute command "
                           << bsoncxx::to_json(m_doc);

                        throw SoftError(ss.str(), error::UNAUTHORIZED);
                    }
                    break;

                default:
                    {
                        ostringstream ss;
                        ss << "unable to change password: " << err.message();

                        throw SoftError(ss.str(), error::INTERNAL_ERROR);
                    }
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        return state;
    }

    State translate_update_grants(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
    {
        uint8_t* pData = mariadb_response.data();
        uint8_t* pEnd = pData + mariadb_response.length();

        pData = translate_revokes(pData, pEnd);
        pData = translate_grants(pData, pEnd);
        mxb_assert(pData == pEnd);

        auto& um = m_database.context().um();

        UserInfo info;
        info.roles = m_new_info.roles;
        uint32_t what = UserInfo::ROLES;

        if (m_what & UserInfo::CUSTOM_DATA)
        {
            info.custom_data = m_new_info.custom_data;
            what |= UserInfo::CUSTOM_DATA;
        }

        if (m_what & UserInfo::MECHANISMS)
        {
            info.mechanisms = m_new_info.mechanisms;
            what |= UserInfo::MECHANISMS;
        }

        if (um.update(m_db, m_user, what, info))
        {
            DocumentBuilder doc;
            doc.append(kvp(key::OK, 1));

            *ppNoSQL_response = create_response(doc.extract());
        }
        else
        {
            ostringstream ss;

            if (m_what & UserInfo::PWD)
            {
                ss << "Could update password both in the MariaDB server and in the local "
                   << "nosqlprotocol database and could ";
            }
            else
            {
                ss << "Could ";
            }

            ss << "update the grants in the MariaDB server, but could not store the corresponing "
               << "roles in the local database.";

            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }

        return State::READY;
    }

    uint8_t* translate_revokes(uint8_t* pData, const uint8_t* pEnd)
    {
        int32_t i = 0;

        while ((i != m_nRevokes) && (pData < pEnd))
        {
            ComResponse response(&pData);

            switch (response.type())
            {
            case ComResponse::OK_PACKET:
                break;

            case ComResponse::ERR_PACKET:
                {
                    ComERR err(response);

                    ostringstream ss;

                    if (m_what & UserInfo::PWD)
                    {
                        ss << "Changing the password succeeded, but revoking privileges with \"";
                    }
                    else
                    {
                        ss << "Revoking privileges with '";
                    }

                    ss << m_statements[i] << "\" failed with \"" << err.message() << "\". "
                       << "The grants in the MariaDB server and the roles in the local "
                       << "nosqlprotocl database are now not in sync.";

                    throw SoftError(ss.str(), error::INTERNAL_ERROR);
                }
                break;

            default:
                mxb_assert(!true);
                throw_unexpected_packet();
            }

            ++i;
        }

        return pData;
    }

    uint8_t* translate_grants(uint8_t* pData, const uint8_t* pEnd)
    {
        int32_t i = 0;

        while ((i != m_nGrants) && (pData < pEnd))
        {
            ComResponse response(&pData);

            switch (response.type())
            {
            case ComResponse::OK_PACKET:
                break;

            case ComResponse::ERR_PACKET:
                {
                    ComERR err(response);

                    ostringstream ss;

                    if (m_what & UserInfo::PWD)
                    {
                        ss << "Changing the password and revoking privileges succeeded, ";
                    }
                    else
                    {
                        ss << "Revoking privileges succeeded, ";
                    }

                    ss << "but granting privileges with \"" << m_statements[i]
                       << "\" failed with \"" << err.message() << "\". "
                       << "The grants in the MariaDB server and the roles in the local "
                       << "nosqlprotocl database are now not in sync.";

                    throw SoftError(ss.str(), error::INTERNAL_ERROR);
                }
                break;

            default:
                mxb_assert(!true);
                throw_unexpected_packet();
            }

            ++i;
        }

        return pData;
    }


private:
    enum class Action
    {
        UPDATE_PASSWORD,
        UPDATE_GRANTS
    };

    Action                   m_action = Action::UPDATE_PASSWORD;
    string                   m_db;
    string                   m_user;
    UserInfo                 m_old_info;
    UserInfo                 m_new_info;
    uint32_t                 m_what { 0 };
    vector<string>           m_statements;
    int32_t                  m_nRevokes { 0 };
    int32_t                  m_nGrants { 0 };
};

// https://docs.mongodb.com/v4.4/reference/command/usersInfo/
class UsersInfo : public UserAdminAuthorize<ImmediateCommand>
{
public:
    static constexpr const char* const KEY = "usersInfo";
    static constexpr const char* const HELP = "";

    using Base = UserAdminAuthorize<ImmediateCommand>;
    using Base::Base;

    void populate_response(DocumentBuilder& doc) override
    {
        auto element = m_doc[KEY];

        switch (element.type())
        {
        case bsoncxx::type::k_utf8:
            get_users(doc, m_database.context().um(), element.get_utf8());
            break;

        case bsoncxx::type::k_array:
            get_users(doc, m_database.context().um(), element.get_array());
            break;

        case bsoncxx::type::k_document:
            get_users(doc, m_database.context().um(), element.get_document());
            break;

        case bsoncxx::type::k_int32:
        case bsoncxx::type::k_int64:
        case bsoncxx::type::k_double:
            {
                int32_t value;
                if (element_as<int32_t>(element, Conversion::RELAXED, &value) && (value == 1))
                {
                    get_users(doc, m_database.context().um());
                    break;
                }
            }
            // fallthrough
        default:
            throw SoftError("User and role names must be either strings or objects", error::BAD_VALUE);
        }
    }

private:
    void get_users(DocumentBuilder& doc, const UserManager& um, const string_view& user_name)
    {
        get_users(doc, um, m_database.name(), string(user_name.data(), user_name.length()));
    }

    void get_users(DocumentBuilder& doc, const UserManager& um, const bsoncxx::array::view& users)
    {
        if (users.empty())
        {
            throw SoftError("$and/$or/$nor must be a nonempty array", error::BAD_VALUE);
        }

        vector<string> mariadb_users;

        for (const auto& element: users)
        {
            switch (element.type())
            {
            case bsoncxx::type::k_utf8:
                {
                    string_view user = element.get_utf8();
                    ostringstream ss;
                    ss << m_database.name() << "." << user;
                    auto mariadb_user = ss.str();

                    mariadb_users.push_back(mariadb_user);
                }
                break;

            case bsoncxx::type::k_document:
                {
                    bsoncxx::document::view doc = element.get_document();

                    string user = get_string(doc, key::USER);
                    string db = get_string(doc, key::DB);

                    auto mariadb_user = db + "." + user;

                    mariadb_users.push_back(mariadb_user);
                }
                break;

            default:
                throw SoftError("User and role names must be either strings or objects", error::BAD_VALUE);
            }
        }

        vector<UserInfo> infos = um.get_infos(mariadb_users);

        add_users(doc, infos);
        doc.append(kvp(key::OK, 1));
    }

    void get_users(DocumentBuilder& doc, const UserManager& um, const bsoncxx::document::view& user)
    {
        auto name = get_string(doc, key::USER);
        auto db = get_string(doc, key::DB);

        get_users(doc, um, db, name);
    }

    void get_users(DocumentBuilder& doc, const UserManager& um)
    {
        vector<UserInfo> infos = um.get_infos(m_database.name());

        add_users(doc, infos);
        doc.append(kvp(key::OK, 1));
    }

    void get_users(DocumentBuilder& doc,
                   const UserManager& um,
                   const string& db,
                   const string& user) const
    {
        ArrayBuilder users;

        UserInfo info;
        if (um.get_info(db, user, &info))
        {
            add_user(users, info);
        }

        doc.append(kvp(key::USERS, users.extract()));
        doc.append(kvp(key::OK, 1));
    }

    static void add_users(DocumentBuilder& doc, const vector<UserInfo>& infos)
    {
        ArrayBuilder users;

        for (const auto& info : infos)
        {
            add_user(users, info);
        }

        doc.append(kvp(key::USERS, users.extract()));
    }

    static void add_user(ArrayBuilder& users, const UserInfo& info)
    {
        ArrayBuilder roles;
        for (const auto& r : info.roles)
        {
            DocumentBuilder role;

            role.append(kvp(key::DB, r.db));
            role.append(kvp(key::ROLE, role::to_string(r.id)));

            roles.append(role.extract());
        }

        ArrayBuilder mechanisms;
        for (const auto& m : info.mechanisms)
        {
            mechanisms.append(scram::to_string(m));
        }

        DocumentBuilder user;
        user.append(kvp(key::_ID, info.mariadb_user));

        uuid_t uuid;
        if (uuid_parse(info.uuid.c_str(), uuid) == 0)
        {
            bsoncxx::types::b_binary user_id;
            user_id.sub_type = bsoncxx::binary_sub_type::k_uuid;
            user_id.bytes = uuid;
            user_id.size = sizeof(uuid);

            user.append(kvp(key::USER_ID, user_id));
        }
        else
        {
            MXS_ERROR("The uuid '%s' of '%s' is invalid.", info.uuid.c_str(), info.mariadb_user.c_str());
        }

        if (!info.custom_data.empty())
        {
            bsoncxx::document::value custom_data = bsoncxx::from_json(info.custom_data);
            user.append(kvp(key::CUSTOM_DATA, custom_data));
        }

        user.append(kvp(key::USER, info.user));
        user.append(kvp(key::DB, info.db));
        user.append(kvp(key::ROLES, roles.extract()));
        user.append(kvp(key::MECHANISMS, mechanisms.extract()));

        users.append(user.extract());
    }

    string get_string(const bsoncxx::document::view& doc, const char* zKey)
    {
        bsoncxx::document::element e = doc[zKey];

        if (!e)
        {
            ostringstream ss;
            ss << "Missing expected field \"" << zKey << "\"";

            throw SoftError(ss.str(), error::NO_SUCH_KEY);
        }

        string s;
        if (!element_as(e, &s))
        {
            ostringstream ss;
            ss << "\"" << zKey << "\" had wrong type. Expected string, found "
               << bsoncxx::to_string(e.type());

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }

        return s;
    }
};

}

}
