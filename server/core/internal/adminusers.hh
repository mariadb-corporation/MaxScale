#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file adminusers.hh - Administration users support routines
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/users.hh>

extern const char* ADMIN_SUCCESS;

void        rest_users_init();
const char* admin_add_inet_user(const char* uname, const char* password, mxs::user_account_type type);
const char* admin_alter_inet_user(const char* uname, const char* password);
const char* admin_remove_inet_user(const char* uname);

mxs::user_account_type admin_inet_user_exists(const char* uname);
mxs::user_account_type admin_verify_inet_user(const char* uname, const char* password);

/**
 * @brief Convert all admin users to JSON
 *
 * @param host Hostname of this server
 *
 * @return Collection of users resources
 */
json_t* admin_all_users_to_json(const char* host);

/**
 * @brief Convert an admin user into JSON
 *
 * @param host Hostname of this server
 * @param user Username to convert
 *
 * @return The user converted to JSON
 */
json_t* admin_user_to_json(const char* host, const char* user);

/**
 * Get the raw admin users data
 *
 * @return The raw JSON that would be stored on disk
 */
mxb::Json admin_raw_users();

/**
 * Load raw admin user JSON
 *
 * @param json JSON to load
 *
 * @return True if the users were loaded and saved on disk successfully
 */
bool admin_load_raw_users(const mxb::Json& json);
