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

//
// https://docs.mongodb.com/v4.4/reference/command/nav-user-management/
//
#include "defs.hh"
#include <uuid/uuid.h>
#include "../nosqlscram.hh"
#include "../nosqlusermanager.hh"

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
        string db = (role.db == "admin" ? "*" : role.db);

        vector<string> privileges;

        switch (role.id)
        {
        case role::Id::DB_ADMIN:
            privileges.push_back("ALTER");
            privileges.push_back("CREATE");
            privileges.push_back("DROP");
            break;

        case role::Id::READ_WRITE:
            privileges.push_back("DELETE");
            privileges.push_back("INSERT");
            privileges.push_back("UPDATE");
        case role::Id::READ:
            privileges.push_back("SELECT");
            break;

        default:
            mxb_assert(!true);
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

}

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/createUser/
class CreateUser final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "createUser";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

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
        bool digest_password = true;
        if (optional(key::DIGEST_PASSWORD, &digest_password) && !digest_password)
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

        m_db = m_database.name();
        m_user = value_as<string>();

        bsoncxx::document::element element;

        element = m_doc[key::PWD];
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

        m_pwd = element.get_utf8();

        bsoncxx::document::view custom_data;
        if (optional(key::CUSTOM_DATA, &custom_data))
        {
            m_custom_data = bsoncxx::to_json(custom_data);
        }

        element = m_doc[key::ROLES];
        if (!element || (element.type() != bsoncxx::type::k_array))
        {
            ostringstream ss;
            ss << "\"createUser\" command requires a \"" << key::ROLES << "\" array";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        role::from_bson(element.get_array(), m_db, &m_roles);

        auto& um = m_database.context().um();

        if (um.user_exists(m_db, m_user))
        {
            ostringstream ss;
            ss << "User \"" << m_user << "@" << m_db << "\" already exists";

            throw SoftError(ss.str(), error::LOCATION51003);
        }
    }

    string generate_sql() override
    {
        string user = "'" + m_db + "." + m_user + "'@'%'";
        string pwd(m_pwd.data(), m_pwd.length());

        m_statements.push_back("CREATE USER " + user + " IDENTIFIED BY '" + pwd + "'");

        auto grants = create_grant_statements(user, m_roles);

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

                MXS_ERROR("Could create user '%s.%s'@'%%', but granting access with the "
                          "statement \"%s\" failed with: (%d) \"%s\". Will now attempt to "
                          "DROP the user.",
                          m_db.c_str(),
                          m_user.c_str(),
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

            auto& um = m_database.context().um();

            vector<uint8_t> salt = crypto::create_random_bytes(scram::SERVER_SALT_SIZE);
            string salt_b64 = mxs::to_base64(salt);

            vector<scram::Mechanism> mechanisms;
            mechanisms.push_back(scram::Mechanism::SHA_1);

            if (um.add_user(m_db, m_user, m_pwd, salt_b64, m_custom_data, mechanisms, m_roles))
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
            sql << "DROP USER '" << m_db << "." << m_user << "'@'%'";

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
                ss << "Could create MariaDB user '" << m_db << "." << m_user << "'@'%', but "
                   << "could not give the required GRANTs. The current used does not have "
                   << "the required privileges. See the MaxScale log for more details.";

                throw SoftError(ss.str(), error::UNAUTHORIZED);
            }
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                ostringstream ss;
                ss << "Could create MariaDB user '" << m_db << "." << m_user << "'@'%', but "
                   << "could not give the required GRANTs and the subsequent attempt to delete "
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

    Action             m_action = Action::CREATE;
    string             m_db;
    string             m_user;
    string_view        m_pwd;
    std::string        m_custom_data;
    vector<role::Role> m_roles;
    vector<string>     m_statements;
    uint32_t           m_dcid = { 0 };
};

// https://docs.mongodb.com/v4.4/reference/command/dropAllUsersFromDatabase/
class DropAllUsersFromDatabase final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "dropAllUsersFromDatabase";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    State execute(GWBUF** ppNoSQL_response) override final
    {
        State state = State::READY;

        const auto& um = m_database.context().um();

        m_db_users = um.get_db_users(m_database.name());

        if (m_db_users.empty())
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
                                string db_user = "'" + m_db_users[i] + "'";
                                users.push_back(db_user);
                            }

                            MXS_WARNING("Dropping users %s succeeded, but dropping '%s' failed: %s",
                                        mxb::join(users, ",").c_str(),
                                        m_db_users[n].c_str(),
                                        err.message().c_str());
                        }
                        break;

                    case ER_CANNOT_USER:
                        MXS_WARNING("User '%s' apparently did not exist in the MariaDB server, even "
                                    "though it should according to the nosqlprotocol book-keeping.",
                                    m_db_users[n].c_str());
                        break;

                    default:
                        MXS_ERROR("Dropping user '%s' failed: %s",
                                  m_db_users[n].c_str(),
                                  err.message().c_str());
                    };
                };
            }
        }

        mxb_assert(pData == pEnd);

        vector<string> users = m_db_users;
        users.resize(n);

        const auto& um = m_database.context().um();

        if (!um.remove_db_users(users))
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
        mxb_assert(!m_db_users.empty());

        vector<string> statements;
        for (const auto& db_user : m_db_users)
        {
            statements.push_back("DROP USER '" + db_user + "'@'%'");
        }

        return mxb::join(statements, ";");
    };

private:
    vector<string> m_db_users;
};

// https://docs.mongodb.com/v4.4/reference/command/dropUser/
class DropUser final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "dropUser";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

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
                        ss << "User \"" << m_user << "@" << m_db << "\" not found";

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
                    ss << "Could remove user \"" << m_user << "@" << m_db << "\" from "
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

        if (!um.user_exists(m_db, m_user))
        {
            ostringstream ss;
            ss << "User \"" << m_user << "@" << m_db << "\" not found";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }
    }

    string generate_sql() override
    {
        ostringstream sql;

        sql << "DROP USER '" << m_db << "." << m_user << "'@'%'";

        return sql.str();
    }

private:
    string m_db;
    string m_user;
};

// https://docs.mongodb.com/v4.4/reference/command/grantRolesToUser/
class GrantRolesToUser : public SingleCommand
{
public:
    static constexpr const char* const KEY = "grantRolesToUser";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

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
        string user = "'" + m_db + "." + m_user + "'@'%'";

        m_statements = create_grant_statements(user, m_roles);

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
class RevokeRolesFromUser : public SingleCommand
{
public:
    static constexpr const char* const KEY = "revokeRolesFromUser";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

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
        string user = "'" + m_db + "." + m_user + "'@'%'";

        m_statements = create_revoke_statements(user, m_roles);

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
class UpdateUser : public SingleCommand
{
public:
    static constexpr const char* const KEY = "updateUser";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    State execute(GWBUF** ppNoSQL_response) override
    {
        State state;

        parse();

        if ((m_what & ~UserInfo::MECHANISMS) != 0)
        {
            // Something else but the mechanisms are updated.
            state = SingleCommand::execute(ppNoSQL_response);
        }
        else
        {
            const auto &um = m_database.context().um();

            if (um.set_mechanisms(m_db, m_user, m_mechanisms))
            {
                DocumentBuilder doc;
                doc.append(kvp(key::OK, 1));

                *ppNoSQL_response = create_response(doc.extract());
                state = State::READY;
            }
            else
            {
                throw SoftError("Could not update the mechanisms.", error::INTERNAL_ERROR);
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
    void parse()
    {
        bool digest_password = true;
        if (optional(key::DIGEST_PASSWORD, &digest_password) && !digest_password)
        {
            ostringstream ss;
            ss << "nosqlprotocol does not support that the client digests the password, "
               << "'digestPassword' must be true.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        m_db = m_database.name();
        m_user = value_as<string>();

        auto& um = m_database.context().um();

        if (!um.get_info(m_db, m_user, &m_info))
        {
            ostringstream ss;
            ss << "Could not find user \"" << m_user << "\" for db \"" << m_db << "\"";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }

        if (optional(key::PWD, &m_pwd))
        {
            m_what |= UserInfo::PWD;
        }

        bsoncxx::document::view custom_data;
        if (optional(key::CUSTOM_DATA, &custom_data))
        {
            m_custom_data = bsoncxx::to_json(custom_data);
            m_what |= UserInfo::CUSTOM_DATA;
        }

        bsoncxx::array::view mechanism_names;
        if (optional(key::MECHANISMS, &mechanism_names))
        {
            scram::from_bson(mechanism_names, &m_mechanisms);

            if (m_what & UserInfo::PWD)
            {
                // Password is changed => mechanisms can be changed-

                auto supported_mechanisms = scram::supported_mechanisms();

                for (const auto mechanism : m_mechanisms)
                {
                    if (supported_mechanisms.count(mechanism) == 0)
                    {
                        ostringstream ss;
                        ss << "Mechanism \"" << scram::to_string(mechanism) << "\" is not supported";

                        throw SoftError(ss.str(), error::BAD_VALUE);
                    }
                }
            }
            else
            {
                // Password is not changed => new mechanisms must be subset of old.
                for (const auto mechanism : m_mechanisms)
                {
                    auto begin = m_info.mechanisms.begin();
                    auto end = m_info.mechanisms.end();

                    if (std::find(begin, end, mechanism) == end)
                    {
                        ostringstream ss;
                        ss << "mechanisms field must be a subset of previously set mechanisms";

                        throw SoftError(ss.str(), error::BAD_VALUE);
                    }
                }
            }

            m_what |= UserInfo::MECHANISMS;
        }
        else
        {
            m_mechanisms = m_info.mechanisms;
        }

        bsoncxx::array::view role_names;
        if (optional(key::ROLES, &role_names))
        {
            role::from_bson(role_names, m_db, &m_roles);

            m_what |= UserInfo::ROLES;
        }

        if (m_what == 0)
        {
            throw SoftError("Must specify at least one field to update in updateUser", error::BAD_VALUE);
        }
    }

    string generate_update_pwd()
    {
        m_action = Action::UPDATE_PASSWORD;

        m_statements.clear();

        string user = "'" + m_db + "." + m_user + "'@'%'";

        mxb_assert(m_what & UserInfo::PWD);

        ostringstream ss;
        ss << "SET PASSWORD FOR " << user << " = PASSWORD('" << m_pwd << "')";

        string s = ss.str();

        m_statements.push_back(s);


        return s;
    }

    string generate_update_grants()
    {
        m_action = Action::UPDATE_GRANTS;

        m_statements.clear();

        string user = "'" + m_db + "." + m_user + "'@'%'";

        auto revokes = create_revoke_statements(user, m_info.roles); // Revoke according to current roles.
        m_nRevokes = revokes.size();

        for (const auto& revoke : revokes)
        {
            m_statements.push_back(revoke);
        }

        auto grants = create_grant_statements(user, m_roles); // Grant according to new roles.
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
                info.pwd = m_pwd;
                uint32_t what = UserInfo::PWD;

                if (m_what & UserInfo::CUSTOM_DATA)
                {
                    info.custom_data = m_custom_data;
                    what |= UserInfo::CUSTOM_DATA;
                }

                if (m_what & UserInfo::MECHANISMS)
                {
                    info.mechanisms = m_mechanisms;
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
                       << "to log in as " << m_user << "@" << m_db << ".";

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
        info.roles = m_roles;
        uint32_t what = UserInfo::ROLES;

        if (m_what & UserInfo::CUSTOM_DATA)
        {
            info.custom_data = m_custom_data;
            what |= UserInfo::CUSTOM_DATA;
        }

        if (m_what & UserInfo::MECHANISMS)
        {
            info.mechanisms = m_mechanisms;
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
    string                   m_pwd;
    string                   m_custom_data;
    vector<scram::Mechanism> m_mechanisms;
    vector<role::Role>       m_roles;
    UserInfo                 m_info;
    uint32_t                 m_what { 0 };
    vector<string>           m_statements;
    int32_t                  m_nRevokes { 0 };
    int32_t                  m_nGrants { 0 };
};

// https://docs.mongodb.com/v4.4/reference/command/usersInfo/
class UsersInfo : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "usersInfo";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

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

        vector<string> db_users;

        for (const auto& element: users)
        {
            switch (element.type())
            {
            case bsoncxx::type::k_utf8:
                {
                    string_view user = element.get_utf8();
                    ostringstream ss;
                    ss << m_database.name() << "." << user;
                    auto db_user = ss.str();

                    db_users.push_back(db_user);
                }
                break;

            case bsoncxx::type::k_document:
                {
                    bsoncxx::document::view doc = element.get_document();

                    string user = get_string(doc, key::USER);
                    string db = get_string(doc, key::DB);

                    auto db_user = db + "." + user;

                    db_users.push_back(db_user);
                }
                break;

            default:
                throw SoftError("User and role names must be either strings or objects", error::BAD_VALUE);
            }
        }

        vector<UserInfo> infos = um.get_infos(db_users);

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
        user.append(kvp(key::_ID, info.db_user));

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
            MXS_ERROR("The uuid '%s' of '%s' is invalid.", info.uuid.c_str(), info.db_user.c_str());
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
