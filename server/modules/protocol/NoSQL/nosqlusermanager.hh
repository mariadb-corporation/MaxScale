/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include "nosqlbase.hh"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <maxbase/json.hh>
#include <maxsql/mariadb_connector.hh>
#include <maxscale/sqlite3.hh>
#include "nosqlscram.hh"

class Configuration;
class SERVER;
class SERVICE;

namespace nosql
{

namespace role
{

enum Id
{
#define NOSQL_ROLE(id, value, name) id = value,
#include "nosqlrole.hh"
#undef NOSQL_ROLE
};

struct Role
{
    std::string db;
    Id          id;
};

std::unordered_map<std::string, uint32_t> to_bitmasks(const std::vector<Role>& roles);

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

std::string to_json_string(const Role& role);
mxb::Json to_json_object(const Role& role);

bool from_json(const mxb::Json& json, Role* pRole);
bool from_json(const std::string& json, Role* pRole);

std::string to_json_string(const std::vector<Role>& roles);
mxb::Json to_json_array(const std::vector<Role>& roles);

bool from_json(const mxb::Json& json, std::vector<Role>* pRoles);
bool from_json(const std::string& json, std::vector<Role>* pRoles);

// throws if invalid.
void from_bson(const bsoncxx::array::view& bson,
               const std::string& default_db,
               std::vector<Role>* pRoles);

}

/**
 * UserManager
 */
class UserManager
{
public:
    UserManager(const UserManager&) = delete;
    UserManager& operator=(const UserManager&) = delete;

    virtual ~UserManager();

    class UserInfo
    {
    public:
        std::string                   mariadb_user;
        std::string                   db;
        std::string                   user;
        std::string                   pwd_sha1_b64;
        std::string                   host;
        std::string                   uuid;
        std::string                   custom_data; // JSON document
        std::string                   salt_sha1_b64;
        std::string                   salt_sha256_b64;
        std::string                   salted_pwd_sha1_b64;
        std::string                   salted_pwd_sha256_b64;
        std::vector<scram::Mechanism> mechanisms;
        std::vector<role::Role>       roles;

        std::vector<uint8_t> pwd_sha1() const;
        std::vector<uint8_t> salt_sha1() const;
        std::vector<uint8_t> salt_sha256() const;
        std::vector<uint8_t> salted_pwd_sha1() const;
        std::vector<uint8_t> salted_pwd_sha256() const;

        std::vector<uint8_t> salt(scram::Mechanism mechanism) const
        {
            return mechanism == scram::Mechanism::SHA_1 ? this->salt_sha1() : this->salt_sha256();
        }

        std::string salt_b64(scram::Mechanism mechanism) const
        {
            return mechanism == scram::Mechanism::SHA_1 ? this->salt_sha1_b64 : this->salt_sha256_b64;
        }

        std::vector<uint8_t> salted_pwd(scram::Mechanism mechanism) const
        {
            return mechanism == scram::Mechanism::SHA_1 ? this->salted_pwd_sha1() : this->salted_pwd_sha256();
        }

        std::string salted_pwd_b64(scram::Mechanism mechanism) const
        {
            return mechanism == scram::Mechanism::SHA_1
                ? this->salted_pwd_sha1_b64
                : this->salted_pwd_sha256_b64;
        }
    };

    virtual bool add_user(const std::string& db,
                          std::string user,
                          std::string password, // Cleartext
                          const std::string& host,
                          const std::string& custom_data, // Assumed to be JSON document.
                          const std::vector<scram::Mechanism>& mechanisms,
                          const std::vector<role::Role>& roles) = 0;

    virtual bool remove_user(const std::string& db, const std::string& user) = 0;

    bool get_info(const std::string& db, const std::string& user, UserInfo* pInfo) const
    {
        return get_info(get_mariadb_user(db, nosql::escape_essential_chars(user)), pInfo);
    }

    virtual bool get_info(const std::string& mariadb_user, UserInfo* pInfo) const = 0;

    bool get_info(const string_view& mariadb_user, UserInfo* pInfo) const
    {
        return get_info(std::string(mariadb_user.data(), mariadb_user.length()), pInfo);
    }

    bool user_exists(const std::string& db, const std::string& user) const
    {
        return get_info(db, user, nullptr);
    }

    bool user_exists(const std::string& db, const string_view& user) const
    {
        return get_info(db, std::string(user.data(), user.length()), nullptr);
    }

    bool user_exists(const std::string& mariadb_user) const
    {
        return get_info(mariadb_user, nullptr);
    }

    bool user_exists(const string_view& mariadb_user) const
    {
        return get_info(std::string(mariadb_user.data(), mariadb_user.length()), nullptr);
    }

    virtual std::vector<UserInfo> get_infos() const = 0;

    virtual std::vector<UserInfo> get_infos(const std::string& db) const = 0;

    virtual std::vector<UserInfo> get_infos(const std::vector<std::string>& mariadb_users) const = 0;

    struct Account
    {
        std::string mariadb_user;
        std::string user;
        std::string db;
        std::string host;
    };

    bool get_account(const std::string& db, const std::string& user, Account* pAccount)
    {
        UserInfo info;
        bool rv = get_info(db, user, &info);

        if (rv)
        {
            pAccount->mariadb_user = info.mariadb_user;
            pAccount->user = info.user;
            pAccount->db = info.db;
            pAccount->host = info.host;
        }

        return rv;
    }

    virtual std::vector<Account> get_accounts(const std::string& db) const = 0;

    virtual bool remove_accounts(const std::vector<Account>& accounts) const = 0;

    struct Update
    {
        enum What
        {
            CUSTOM_DATA = 1 << 0,
            MECHANISMS  = 1 << 1,
            PWD         = 1 << 2,
            ROLES       = 1 << 3,

            MASK = (PWD | MECHANISMS | ROLES | CUSTOM_DATA)
        };

        std::string                   pwd;
        std::string                   custom_data;
        std::vector<scram::Mechanism> mechanisms;
        std::vector<role::Role>       roles;
    };

    virtual bool update(const std::string& db,
                        const std::string& user,
                        uint32_t what,
                        const Update& data) const = 0;

    bool set_mechanisms(const std::string& db,
                        const std::string& user,
                        const std::vector<scram::Mechanism>& mechanisms) const
    {
        Update data;
        data.mechanisms = mechanisms;

        return update(db, user, Update::MECHANISMS, data);
    }

    bool set_roles(const std::string& db, const std::string& user, const std::vector<role::Role>& roles) const
    {
        Update data;
        data.roles = roles;

        return update(db, user, Update::ROLES, data);
    }

    static std::string get_mariadb_user(const std::string& db, const std::string& user)
    {
        return db + "." + user;
    }

    static std::string get_mariadb_user(const std::string& db, const string_view& user)
    {
        return db + "." + std::string(user.data(), user.length());
    }

protected:
    UserManager()
    {
    }

    struct AddUser
    {
        std::string mariadb_user;
        std::string db;
        std::string user;
        std::string pwd;
        std::string host;
        std::string salt_sha1_b64;
        std::string salted_pwd_sha1_b64;
        std::string salt_sha256_b64;
        std::string salted_pwd_sha256_b64;
        std::string pwd_sha1_b64;
        std::string uuid;
    };

    AddUser get_add_user_data(const std::string& db,
                              std::string user,
                              std::string pwd,
                              const std::string& host,
                              const std::vector<scram::Mechanism>& mechanisms);
};

/**
 * UserManagerSqlite3
 */
class UserManagerSqlite3 : public UserManager
{
public:
    ~UserManagerSqlite3();

    static std::unique_ptr<UserManager> create(const std::string& name);

    const std::string& path() const
    {
        return m_path;
    }

    bool add_user(const std::string& db,
                  std::string user,
                  std::string password, // Cleartext
                  const std::string& host,
                  const std::string& custom_data, // Assumed to be JSON document.
                  const std::vector<scram::Mechanism>& mechanisms,
                  const std::vector<role::Role>& roles) override;

    bool remove_user(const std::string& db, const std::string& user) override;

    using UserManager::get_info;
    bool get_info(const std::string& mariadb_user, UserInfo* pInfo) const override;

    std::vector<UserInfo> get_infos() const override;

    std::vector<UserInfo> get_infos(const std::string& db) const override;

    std::vector<UserInfo> get_infos(const std::vector<std::string>& mariadb_users) const override;

    std::vector<Account> get_accounts(const std::string& db) const override;

    bool remove_accounts(const std::vector<Account>& accounts) const override;

    bool update(const std::string& db,
                const std::string&
                user, uint32_t what,
                const Update& data) const override;

private:
    UserManagerSqlite3(std::string path, sqlite3* pDb);

    std::string m_path;
    sqlite3&    m_db;
};

/**
 * UserManagerMariaDB
 */
class UserManagerMariaDB : public UserManager
{
public:
    static std::unique_ptr<UserManager> create(std::string name,
                                               SERVICE* pService,
                                               const Configuration* pConfig);

    bool add_user(const std::string& db,
                  std::string user,
                  std::string password, // Cleartext
                  const std::string& host,
                  const std::string& custom_data, // Assumed to be JSON document.
                  const std::vector<scram::Mechanism>& mechanisms,
                  const std::vector<role::Role>& roles) override;

    bool remove_user(const std::string& db, const std::string& user) override;

    using UserManager::get_info;
    bool get_info(const std::string& mariadb_user, UserInfo* pInfo) const override;

    std::vector<UserInfo> get_infos() const override;

    std::vector<UserInfo> get_infos(const std::string& db) const override;

    std::vector<UserInfo> get_infos(const std::vector<std::string>& mariadb_users) const override;

    std::vector<Account> get_accounts(const std::string& db) const override;

    bool remove_accounts(const std::vector<Account>& accounts) const override;

    bool update(const std::string& db,
                const std::string&
                user, uint32_t what,
                const Update& data) const override;

private:
    UserManagerMariaDB(std::string name, SERVICE* pService, const Configuration* pConfig);

    bool check_connection() const;
    bool prepare_server() const;

    bool user_info_from_result(mxq::QueryResult* pResult, UserManager::UserInfo* pInfo) const;
    std::vector<UserManager::UserInfo> user_infos_from_result(mxq::QueryResult* pResult) const;

    bool do_add_user(const std::string& db,
                     std::string user,
                     std::string password, // Cleartext
                     const std::string& host,
                     const std::string& custom_data, // Assumed to be JSON document.
                     const std::vector<scram::Mechanism>& mechanisms,
                     const std::vector<role::Role>& roles);

    bool do_remove_user(const std::string& db, const std::string& user);

    bool do_get_info(const std::string& mariadb_user, UserInfo* pInfo) const;

    std::vector<UserInfo> do_get_infos() const;

    std::vector<UserInfo> do_get_infos(const std::string& db) const;

    std::vector<UserInfo> do_get_infos(const std::vector<std::string>& mariadb_users) const;

    std::vector<Account> do_get_accounts(const std::string& db) const;

    bool do_remove_accounts(const std::vector<Account>& accounts) const;

    bool do_update(const std::string& db,
                   const std::string&
                   user, uint32_t what,
                   const Update& data) const;

    std::string             m_name;
    std::string             m_table;
    SERVICE&                m_service;
    const Configuration&    m_config;
    mutable SERVER*         m_pServer { nullptr };
    mutable maxsql::MariaDB m_db;
    mutable std::mutex      m_mutex;
};

}
