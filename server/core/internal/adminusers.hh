#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * @file adminusers.hh - Administration users support routines
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/users.hh>

extern const char* ADMIN_SUCCESS;

void rest_users_init();
const char* admin_add_inet_user(const char* uname, const char* password, mxs::user_account_type type);
const char* admin_alter_inet_user(const char* uname, const char* password);
const char* admin_remove_inet_user(const char* uname);
bool admin_inet_user_exists(const char* uname);
bool admin_verify_inet_user(const char* uname, const char* password);
bool admin_user_is_inet_admin(const char* username, const char* password);

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
 * Check if user credentials are accepted by any of the configured REST API PAM services. By default, both
 * the read-only and read-write services are attempted.
 *
 * @param username Username
 * @param password Password
 * @param min_acc_type Minimum account type required. If BASIC, authentication succeeds if
 * either read-only or readwrite service succeeds. If ADMIN, only the readwrite service is attempted.
 * @return True if user & password logged in successfully
 */
bool admin_user_is_pam_account(const std::string& username,
    const std::string& password,
    mxs::user_account_type min_acc_type = mxs::USER_ACCOUNT_BASIC);
