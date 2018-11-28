/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file users.h The functions to manipulate a set of administrative users
 */

#include <maxscale/cdefs.h>
#include <maxbase/jansson.h>
#include <maxscale/dcb.h>
#include <maxscale/listener.h>
#include <maxscale/service.hh>
#include <openssl/sha.h>

MXS_BEGIN_DECLS

/** User account types */
enum user_account_type
{
    USER_ACCOUNT_UNKNOWN,
    USER_ACCOUNT_BASIC,     /**< Allows read-only access */
    USER_ACCOUNT_ADMIN      /**< Allows complete access */
};

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
bool users_add(USERS* users, const char* user, const char* password, enum user_account_type type);

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
 * Check if user is an administrator
 *
 * @param users The users table
 * @param user  User to check
 *
 * @return True if user is an administrator
 */
bool users_is_admin(USERS* users, const char* user);

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
 * @brief Default user loading function
 *
 * A generic key-value user table is allocated for the service.
 *
 * @param port Listener configuration
 * @return Always AUTH_LOADUSERS_OK
 */
int users_default_loadusers(SERV_LISTENER* port);

/**
 * @brief Default authenticator diagnostic function
 *
 * @param dcb DCB where data is printed
 * @param port Port whose data is to be printed
 */
void users_default_diagnostic(DCB* dcb, SERV_LISTENER* port);

/**
 * @brief Default authenticator diagnostic function
 *
 * @param port Port whose data is to be printed
 */
json_t* users_default_diagnostic_json(const SERV_LISTENER* port);

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
json_t* users_diagnostic_json(USERS* users);

/**
 * Convert account_type to a string
 *
 * @param type Enum value
 *
 * @return String representation of @c type
 */
const char* account_type_to_str(enum user_account_type type);

/**
 * Convert JSON value to account_type value
 *
 * @param json JSON value to convert
 *
 * @return Enum value of @c json
 */
enum user_account_type json_to_account_type(json_t* json);

MXS_END_DECLS
