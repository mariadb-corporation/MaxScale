/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <string>

namespace maxbase
{
namespace pam
{
struct AuthResult
{
    enum class Result
    {
        SUCCESS,                /**< Authentication succeeded */
        WRONG_USER_PW,          /**< Username or password was wrong */
        ACCOUNT_INVALID,        /**< pam_acct_mgmt returned error */
        MISC_ERROR              /**< Miscellaneous error */
    };

    Result      type {Result::MISC_ERROR};
    std::string error;
};

enum class AuthMode
{
    PW,         /**< Password only */
    PW_2FA      /**< Password + 2FA code */
};

struct UserData
{
    std::string username;   /**< Username */
    std::string remote;     /**< Client remote address */
};

/**
 * Passwords given by client
 */
struct PwdData
{
    std::string password;
    std::string two_fa_code;
};

/**
 * Password prompts expected from PAM api. If these values are empty, the prompts are not checked.
 */
struct ExpectedMsgs
{
    std::string password_query;
    std::string two_fa_query;
};

/**
 * Check if the user & password can log into the given PAM service. This function will block until the
 * operation completes.
 *
 * @param user Username
 * @param password Password
 * @param service Which PAM service is the user logging to
 * @return A result struct with the result and an error message.
 */
AuthResult
authenticate(const std::string& user, const std::string& password, const std::string& service);

/**
 * Check if the user & password can log into the given PAM service. This function will block until the
 * operation completes.
 *
 * @param user Username
 * @param password Password
 * @param client_remote Client address, used for logging
 * @param service Which PAM service is the user logging to
 * @param expected_msg The first expected message from the PAM authentication system.
 * Typically "Password: ". If set to empty, the message is not checked.
 * @return A result struct with the result and an error message.
 */
AuthResult
authenticate(const std::string& user, const std::string& password, const std::string& client_remote,
             const std::string& service, const std::string& expected_msg);

AuthResult
authenticate(AuthMode mode, const UserData& user, const PwdData& pwds, const std::string& service,
             const ExpectedMsgs& exp_msgs);
}
}
