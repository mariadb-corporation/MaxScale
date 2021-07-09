/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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
extern const std::string EXP_PW_QUERY;      /* Expected normal password query */

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

    /**
     * The username after authentication when user mapping is enabled. Can be different from the input
     * username if a pam module changes username during authentication. */
    std::string mapped_user;
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

struct AuthSettings
{
    std::string service;    /**< Pam service to log in */

    /**
     * Enable if the pam service may map the input username to something else. When true, MaxScale will
     * fetch the mapped username and will not run 'pam_acct_mgmt' after authentication, as it would likely
     * fail. */
    bool mapping_on {false};
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
 * Authenticate user into the given PAM service. This function will block until the
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
 * Authenticate user into the given PAM service. This function will block until the
 * operation completes.
 *
 * @param mode Password mode
 * @param user Username & remote host
 * @param pwds Passwords given by user
 * @param sett Pam service and other settings
 * @param exp_msgs Password queries expected from PAM api
 * @return A result struct with the result and an error message.
 */
AuthResult
authenticate(AuthMode mode, const UserData& user, const PwdData& pwds, const AuthSettings& sett,
             const ExpectedMsgs& exp_msgs);

/**
 * Does pam prompt match the expected message? The prompt matches if the prompt begins with the expected
 * message, compared case insensitively.
 *
 * @param prompt Prompt from PAM api or backend server
 * @param expected_start Expected start of prompt
 * @return True on match
 */
bool match_prompt(const char* prompt, const std::string& expected_start);
}
}
