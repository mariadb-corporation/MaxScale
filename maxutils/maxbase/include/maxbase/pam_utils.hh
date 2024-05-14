/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <string>
#include <optional>
#include <tuple>
#include <vector>

namespace maxbase
{
namespace pam
{
extern const std::string EXP_PW_QUERY;      /* Expected normal password query */

constexpr uint8_t SBOX_CONV = 'C';
constexpr uint8_t SBOX_AUTHENTICATED_AS = 'A';
constexpr uint8_t SBOX_EOF = 'E';
constexpr uint8_t SBOX_WARN = 'W';

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
    PW_2FA,     /**< Password + 2FA code */
    SUID        /**< Suid wrapper, supports 2FA */
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
 * @param service Pam service
 * @param exp_msgs Password queries expected from PAM api
 * @return A result struct with the result and an error message.
 */
AuthResult
authenticate(AuthMode mode, const UserData& user, const PwdData& pwds, const std::string& service,
             const ExpectedMsgs& exp_msgs);

/**
 * Authenticate user into the given PAM service. This function will block until the
 * operation completes. The function will write pam prompts and messages and read
 * responses to/from the given file descriptors. Typically called via subprocess.
 *
 * @param read_fd Fd for reading input
 * @param write_fd Fd for writing output
 * @param user Username & remote host
 * @param service Pam service
 * @return Result structure
 */
AuthResult authenticate_fd(int read_fd, int write_fd, const UserData& user, const std::string& service);

/**
 * Does pam prompt match the expected message? The prompt matches if the prompt begins with the expected
 * message, compared case insensitively.
 *
 * @param prompt Prompt from PAM api or backend server
 * @param expected_start Expected start of prompt
 * @return True on match
 */
bool match_prompt(const char* prompt, const std::string& expected_start);

/**
 * Read a length-encoded string from pipe, blocking to wait until data can be read. Should be only used on
 * blocking pipes, typically by a subprocess.
 *
 * @param fd File descriptor
 * @return String contents on success.
 */
std::optional<std::string> read_string_blocking(int fd);

/**
 * Prepare a string to be written to a pipe. Prepends the string length, then appends the string.
 *
 * @param str The string to write
 * @param out Output vector
 */
void add_string(std::string_view str, std::vector<uint8_t>* out);

enum class Debug {YES, NO};
/**
 * Generate suid tool execute command.
 *
 * @param debug Debug messages enabled
 * @return The command, or empty on error
 */
std::string gen_auth_tool_run_cmd(Debug debug = Debug::NO);

/**
 * Create a message containing settings for the suid process.
 *
 * @param user Username to authenticate
 * @param service Pam service
 * @return The message
 */
std::vector<uint8_t> create_suid_settings_msg(std::string_view user, std::string_view service);

/**
 * Extract next message from message buffer.
 *
 * @param msg_buf Buffer with message(s)
 * @return Message type and message. Type is -1 on error and 0 if buffer does not have a complete message.
 */
std::tuple<int, std::string> next_message(std::string& msg_buf);
}
}
