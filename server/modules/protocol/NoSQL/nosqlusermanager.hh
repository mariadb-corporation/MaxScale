/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include "nosqlbase.hh"
#include <memory>
#include <maxbase/json.hh>
#include <maxscale/sqlite3.h>
#include "nosqlscram.hh"

namespace nosql
{

namespace role
{

enum class Id
{
#define NOSQL_ROLE(id, name) id,
#include "nosqlrole.hh"
#undef NOSQL_ROLE
};

struct Role
{
    std::string db;
    Id          id;
};

std::string to_string(Id id);

bool from_string(const std::string& key, Id* pId);

inline bool from_string(const char* zKey, Id* pValue)
{
    return from_string(std::string(zKey), pValue);
}

inline bool from_string(const string_view& key, Id* pValue)
{
    return from_string(std::string(key.data(), key.length()), pValue);
}

std::string to_json(const Role& role);

bool from_json(const mxb::Json& json, Role* pRole);
bool from_json(const std::string& json, Role* pRole);

std::string to_json(const std::vector<Role>& roles);

bool from_json(const std::string& json, std::vector<Role>* pRoles);

// throws if invalid.
void from_bson(const bsoncxx::array::view& bson,
               const std::string& default_db,
               std::vector<Role>* pRoles);

}

class UserManager
{
public:
    ~UserManager();

    class UserInfo
    {
    public:
        enum What
        {
            CUSTOM_DATA = 1 << 0,
            MECHANISMS  = 1 << 1,
            PWD         = 1 << 2,
            ROLES       = 1 << 3,

            MASK = (PWD | MECHANISMS | ROLES | CUSTOM_DATA)
        };

        std::string                   db_user;
        std::string                   db;
        std::string                   user;
        std::string                   pwd;
        std::string                   uuid;
        std::vector<uint8_t>          salt;
        std::string                   custom_data; // JSON document
        std::string                   salt_b64;
        std::vector<scram::Mechanism> mechanisms;
        std::vector<role::Role>       roles;
    };

    static std::unique_ptr<UserManager> create(const std::string& name);

    const std::string& path() const
    {
        return m_path;
    }

    bool add_user(const std::string& db,
                  const string_view& user,
                  const string_view& pwd,
                  const std::string& custom_data, // Assumed to be JSON document.
                  const std::vector<scram::Mechanism>& mechanisms,
                  const std::vector<role::Role>& roles);

    bool remove_user(const std::string& db, const std::string& user);

    bool get_info(const std::string& db, const std::string& user, UserInfo* pInfo) const;

    bool get_info(const std::string& db_user, UserInfo* pInfo) const;

    bool get_pwd(const std::string& db, const std::string& user, std::string* pPwd) const;

    bool get_salt_b64(const std::string& db, const std::string& user, std::string* pSalt_b64) const;

    bool user_exists(const std::string& db, const std::string& user) const
    {
        return get_info(db, user, nullptr);
    }

    bool user_exists(const std::string& db, const string_view& user) const
    {
        return get_info(db, std::string(user.data(), user.length()), nullptr);
    }

    bool user_exists(const std::string& db_user) const
    {
        return get_info(db_user, nullptr);
    }

    bool user_exists(const string_view& db_user) const
    {
        return get_info(std::string(db_user.data(), db_user.length()), nullptr);
    }

    std::vector<UserInfo> get_infos() const;

    std::vector<UserInfo> get_infos(const std::string& db) const;

    std::vector<UserInfo> get_infos(const std::vector<std::string>& db_users) const;

    std::vector<std::string> get_db_users(const std::string& db) const;

    bool remove_db_users(const std::vector<std::string>& db_users) const;

    bool update(const std::string& db, const std::string& user, uint32_t what, const UserInfo& info) const;

    bool set_mechanisms(const std::string& db,
                        const std::string& user,
                        const std::vector<scram::Mechanism>& mechanisms) const
    {
        UserInfo info;
        info.mechanisms = mechanisms;

        return update(db, user, UserInfo::MECHANISMS, info);
    }

    bool set_roles(const std::string& db, const std::string& user, const std::vector<role::Role>& roles) const
    {
        UserInfo info;
        info.roles = roles;

        return update(db, user, UserInfo::ROLES, info);
    }

    static std::string get_db_user(const std::string& db, const std::string& user)
    {
        return db + "." + user;
    }

    static std::string get_db_user(const std::string& db, const string_view& user)
    {
        return db + "." + std::string(user.data(), user.length());
    }

private:
    UserManager(std::string path, sqlite3* pDb);

    std::string m_path;
    sqlite3&    m_db;
};

}
