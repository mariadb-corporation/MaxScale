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
#include <map>
#include "../nosqlscram.hh"
#include "../nosqlusermanager.hh"

using namespace std;

namespace nosql
{

namespace role
{

enum class Id
{
    DB_ADMIN,
    READ,
    READ_WRITE
};

const map<string, Id> roles =
{
    { "dbAdmin",   Id::DB_ADMIN },
    { "read",      Id::READ },
    { "readWrite", Id::READ_WRITE }
};

string to_string(Id id)
{
    for (const auto& kv : roles)
    {
        if (id == kv.second)
        {
            return kv.first;
        }
    }

    mxb_assert(!true);

    return "unknown";
}

bool from_string(const string& key, Id* pValue)
{
    auto it = roles.find(key);

    bool found = (it != roles.end());

    if (found)
    {
        *pValue = it->second;
    }

    return found;
}

bool from_string(const char* zKey, Id* pValue)
{
    return from_string(string(zKey), pValue);
}

bool from_string(const string_view& key, Id* pValue)
{
    return from_string(string(key.data(), key.length()), pValue);
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
        ComResponse response(mariadb_response.data());

        DocumentBuilder doc;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            {
                auto& um = m_database.context().um();

                vector<uint8_t> salt = crypto::create_random_bytes(scram::SERVER_SALT_SIZE);
                string salt_b64 = mxs::to_base64(salt);

                if (um.add_user(m_scope, m_user, m_pwd, salt_b64, m_roles))
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
            }
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

        *ppNoSQL_response = create_response(doc.extract());
        return State::READY;
    }

protected:
    void prepare() override
    {
        m_scope = m_database.name();
        m_user += value_as<string>();

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

        element = m_doc[key::ROLES];
        if (!element || (element.type() != bsoncxx::type::k_array))
        {
            ostringstream ss;
            ss << "\"createUser\" command requires a \"" << key::ROLES << "\" array";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        check_roles(element.get_array());

        auto& um = m_database.context().um();

        if (um.user_exists(m_scope, m_user))
        {
            ostringstream ss;
            ss << "User \"" << m_user << "@" << m_scope << "\" already exists";

            throw SoftError(ss.str(), error::LOCATION51003);
        }
    }

    string generate_sql() override
    {
        ostringstream sql;

        sql << "CREATE USER '" << m_scope << "." << m_user << "'@'%' IDENTIFIED BY '" << m_pwd << "'";

        return sql.str();
    }

private:
    void add_role(const string& db, role::Id role_id)
    {
        m_role_ids.push_back(make_pair(db, role_id));
    }

    void check_role(const string_view& role_name, const string& db)
    {
        role::Id role_id;
        if (!role::from_string(role_name, &role_id))
        {
            ostringstream ss;
            ss << "No role named " << role_name << "@" << db;

            throw SoftError(ss.str(), error::ROLE_NOT_FOUND);
        }

        add_role(db, role_id);
    }

    void check_role(const string_view& role_name, const string_view& db)
    {
        check_role(role_name, string(db.data(), db.length()));
    }

    void check_role(const string_view& role_name)
    {
        return check_role(role_name, m_database.name());
    }

    void check_role(const bsoncxx::document::view& role_doc)
    {
        auto e = role_doc[key::ROLE];
        if (!e)
        {
            throw SoftError("Missing expected field \"role\"", error::NO_SUCH_KEY);
        }

        if (e.type() != bsoncxx::type::k_utf8)
        {
            ostringstream ss;
            ss << "\"role\" had the wrong type. Expected string, found " << bsoncxx::to_string(e.type());
            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }

        string_view role_name = e.get_utf8();

        e = role_doc[key::DB];
        if (!e)
        {
            throw SoftError("Missing expected field \"db\"", error::NO_SUCH_KEY);
        }

        if (e.type() != bsoncxx::type::k_utf8)
        {
            ostringstream ss;
            ss << "\"db\" had the wrong type. Expected string, found " << bsoncxx::to_string(e.type());
            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }

        string_view db = e.get_utf8();

        check_role(role_name, db);
    }

    void check_roles(const bsoncxx::array::view& roles)
    {
        for (const auto& element : roles)
        {
            switch (element.type())
            {
            case bsoncxx::type::k_utf8:
                check_role(element.get_utf8());
                break;

            case bsoncxx::type::k_document:
                check_role(element.get_document());
                break;

            default:
                throw SoftError("Role names must be either strings or objects", error::BAD_VALUE);
            }
        }
    }

private:
    string                        m_scope;
    string                        m_user;
    string_view                   m_pwd;
    bsoncxx::array::view          m_roles;
    vector<pair<string,role::Id>> m_role_ids;
};

// https://docs.mongodb.com/v4.4/reference/command/dropAllUsersFromDatabase/
class DropAllUsersFromDatabase final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "dropAllUsersFromDatabase";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        doc.append(kvp(key::N, 0));
        doc.append(kvp(key::OK, 1));
    }
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
                        ss << "User \"" << m_user << "@" << m_scope << "\" not found";

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

                if (um.remove_user(m_scope, m_user))
                {
                    doc.append(kvp("ok", 1));
                }
                else
                {
                    ostringstream ss;
                    ss << "Could remove user \"" << m_user << "@" << m_scope << "\" from "
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
        m_scope = m_database.name();
        m_user = value_as<string>();

        auto& um = m_database.context().um();

        if (!um.user_exists(m_scope, m_user))
        {
            ostringstream ss;
            ss << "User \"" << m_user << "@" << m_scope << "\" not found";

            throw SoftError(ss.str(), error::USER_NOT_FOUND);
        }
    }

    string generate_sql() override
    {
        ostringstream sql;

        sql << "DROP USER '" << m_scope << "." << m_user << "'@'%'";

        return sql.str();
    }

private:
    string m_scope;
    string m_user;
};

// https://docs.mongodb.com/v4.4/reference/command/grantRolesToUser/

// https://docs.mongodb.com/v4.4/reference/command/revokeRolesFromUser/

// https://docs.mongodb.com/v4.4/reference/command/updateUser/

// https://docs.mongodb.com/v4.4/reference/command/usersInfo/


}

}
