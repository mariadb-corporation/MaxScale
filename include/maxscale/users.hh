/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
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

    static Users* from_json(json_t* json);

    bool add(const std::string& user, const std::string& password, user_account_type perm);
    bool remove(const std::string& user);
    bool get(const std::string& user, UserInfo* output = NULL) const;
    bool authenticate(const std::string& user, const std::string& password);
    int  admin_count() const;
    bool check_permissions(const std::string& user, const std::string& password,
                           user_account_type perm) const;
    bool    set_permissions(const std::string& user, user_account_type perm);
    json_t* diagnostics() const;
    void    diagnostic(DCB* dcb) const;
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
    void        load_json(json_t* json);
    std::string hash(const std::string& password);
    std::string old_hash(const std::string& password);

    mutable std::mutex m_lock;
    UserMap            m_data;
};
}
/**
 * An opaque users object
 */
typedef struct users
{
} USERS;

/**
 * Allocate a new users table
 *
 * @return The users table or NULL if memory allocation failed
 */
USERS* users_alloc();

/**
 * Free a users table
 *
 * @param users Users table to free
 */
void users_free(USERS* users);

/**
 * Add a new user to the user table. The user name must be unique
 *
 * @param users    The users table
 * @param user     The user name
 * @param password The password for the user
 * @param type     The type of account to create
 *
 * @return True if user was added
 */
bool users_add(USERS* users, const char* user, const char* password, mxs::user_account_type type);

/**
 * Delete a user from the user table.
 *
 * @param users         The users table
 * @param user          The user name
 *
 * @return True if user was deleted
 */
bool users_delete(USERS* users, const char* user);

/**
 * Authenticate a user
 *
 * @param users The users table
 * @param user  The user name
 * @param pw    The password sent by the user
 *
 * @return True if authentication data matched the stored value
 */
bool users_auth(USERS* users, const char* user, const char* password);

/**
 * Check if a user exists
 *
 * @param users The users table
 * @param user  User to find
 *
 * @return True if user exists
 */
bool users_find(USERS* users, const char* user);

/**
 * Change password for a user
 *
 * @param users    The users table
 * @param user     User to alter
 * @param password The new password for the user
 *
 * @return True if password was changed
 */
bool users_change_password(USERS* users, const char* user, const char* password);

/**
 * Check if user is an administrator
 *
 * @param users    The users table
 * @param user     User to check
 * @param password Password of the user or NULL if password isn't available
 *
 * @return True if user is an administrator
 */
bool users_is_admin(USERS* users, const char* user, const char* password);

/**
 * Check how many admin account exists
 *
 * @param users Users to check
 *
 * @return Number of admin accounts
 */
int users_admin_count(USERS* users);

/**
 * Dump users as JSON
 *
 * The resulting JSON can be loaded later to restore the users.
 *
 * @param users Users to dump
 *
 * @return JSON form of the users that can be used for serialization
 */
json_t* users_to_json(USERS* users);

/**
 * Load users from JSON
 *
 * @param json JSON to load
 *
 * @return The loaded users
 */
USERS* users_from_json(json_t* json);

/**
 * Print users to a DCB
 *
 * @param dcb   DCB where users are printed
 * @param users Users to print
 */
void users_diagnostic(DCB* dcb, USERS* users);

/**
 * Convert users to JSON
 *
 * @param users Users to convert
 *
 * @return JSON version of users
 */
json_t* users_diagnostics(USERS* users);

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
