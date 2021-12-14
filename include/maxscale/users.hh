/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

/**
 * @file users.h The functions to manipulate a set of administrative users
 */

#include <maxscale/ccdefs.hh>
#include <openssl/sha.h>
#include <maxbase/jansson.h>
#include <maxscale/dcb.hh>
#include <maxscale/service.hh>

namespace maxscale
{

/** User account types */
enum user_account_type
{
    USER_ACCOUNT_UNKNOWN,
    USER_ACCOUNT_BASIC,     /**< Allows read-only access */
    USER_ACCOUNT_ADMIN      /**< Allows complete access */
};

struct UserInfo
{
    UserInfo() = default;
    UserInfo(std::string pw, user_account_type perm)
        : password(pw)
        , permissions(perm)
    {
    }

    std::string       password;
    user_account_type permissions {USER_ACCOUNT_BASIC};
};


class Users
{
public:
    using UserMap = std::unordered_map<std::string, UserInfo>;

    Users() = default;
    Users(const Users& rhs);
    Users(Users&& rhs) noexcept;
    Users& operator=(const Users& rhs);
    Users& operator=(Users&& rhs) noexcept;

    void load_json(json_t* json);
    bool add(const std::string& user, const std::string& password, user_account_type perm);
    bool remove(const std::string& user);
    bool get(const std::string& user, UserInfo* output = NULL) const;
    bool authenticate(const std::string& user, const std::string& password);
    int  admin_count() const;
    bool check_permissions(const std::string& user, const std::string& password,
                           user_account_type perm) const;
    bool    set_permissions(const std::string& user, user_account_type perm);
    json_t* diagnostics() const;
    bool    empty() const;
    json_t* to_json() const;

    /**
     * Return a copy of the data.
     *
     * @return Data copy
     */
    UserMap copy_contents() const;

private:
    static bool is_admin(const UserMap::value_type& value);

    bool        add_hashed(const std::string& user, const std::string& password, user_account_type perm);
    std::string hash(const std::string& password);
    std::string old_hash(const std::string& password);

    mutable std::mutex m_lock;
    UserMap            m_data;
};
}

/**
 * Change password for a user
 *
 * @param users    The users table
 * @param user     User to alter
 * @param password The new password for the user
 *
 * @return True if password was changed
 */
bool users_change_password(mxs::Users* users, const char* user, const char* password);

/**
 * Check if user is an administrator
 *
 * @param users    The users table
 * @param user     User to check
 * @param password Password of the user or NULL if password isn't available
 *
 * @return True if user is an administrator
 */
bool users_is_admin(mxs::Users* users, const char* user, const char* password);

/**
 * Convert account_type to a string
 *
 * @param type Enum value
 *
 * @return String representation of @c type
 */
const char* account_type_to_str(mxs::user_account_type type);

/**
 * Convert JSON value to account_type value
 *
 * @param json JSON value to convert
 *
 * @return Enum value of @c json
 */
mxs::user_account_type json_to_account_type(json_t* json);
