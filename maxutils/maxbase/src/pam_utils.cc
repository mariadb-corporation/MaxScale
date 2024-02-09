/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <maxbase/pam_utils.hh>

#include <cstring>
#include <security/pam_appl.h>
#include <unistd.h>
#include <climits>
#include <libgen.h>
#include <maxbase/alloc.hh>
#include <maxbase/string.hh>
#include <maxbase/assert.hh>
#include <maxbase/format.hh>

using std::string;
using std::string_view;

namespace
{

/** Used by the PAM conversation function */
struct ConversationData
{
    const mxb::pam::AuthMode      mode {mxb::pam::AuthMode::PW};
    const mxb::pam::UserData*     userdata {nullptr};
    const mxb::pam::PwdData*      pwds {nullptr};
    const mxb::pam::ExpectedMsgs* exp_msgs {nullptr};

    int prompt_ind {0};     /**< How many passwords have been given */

    ConversationData(mxb::pam::AuthMode mode, const mxb::pam::UserData* userdata,
                     const mxb::pam::PwdData* pwds, const mxb::pam::ExpectedMsgs* exp_msgs)
        : mode(mode)
        , userdata(userdata)
        , pwds(pwds)
        , exp_msgs(exp_msgs)
    {
    }
};
int conversation_func(int num_msg, const struct pam_message** messages, struct pam_response** responses_out,
                      void* appdata_ptr);

// String length type. Real max value is 10k.
using LengthType = int16_t;
const size_t length_size = sizeof(LengthType);

struct ConvDataFd
{
    ConvDataFd(int read_fd, int write_fd)
        : read_fd(read_fd)
        , write_fd(write_fd)
    {
    }

    int read_fd {-1};
    int write_fd {-1};

    // Contains all pam messages gathered so far, waiting to be sent to the main process.
    // Must be stored outside the conv function to preserve its value between calls.
    std::string message_buffer;
};

int conversation_func_fd(int n_msg, const pam_message** messages, pam_response** responses_out,
                         void* appdata_ptr);
std::optional<string> roundtrip(int fd_in, int fd_out, string_view message);

mxb::pam::AuthResult
authenticate(const pam_conv* conv, const mxb::pam::UserData& user, const string& service);

std::tuple<int, string> extract_string(const char* ptr, const char* end);
}

namespace maxbase
{
namespace pam
{
const std::string EXP_PW_QUERY = "Password";

AuthResult authenticate(AuthMode mode, const UserData& user, const PwdData& pwds, const string& service,
                        const ExpectedMsgs& exp_msgs)
{
    ConversationData appdata(mode, &user, &pwds, &exp_msgs);
    pam_conv conv_struct = {conversation_func, &appdata};
    return ::authenticate(&conv_struct, user, service);
}

AuthResult authenticate(const string& user, const string& password, const string& service)
{
    UserData usr = {user, ""};
    PwdData pwds = {password, ""};
    ExpectedMsgs exp_msg = {EXP_PW_QUERY, ""};
    return authenticate(AuthMode::PW, usr, pwds, service, exp_msg);
}

AuthResult authenticate_fd(int read_fd, int write_fd, const UserData& user, const string& service)
{
    ConvDataFd appdata(read_fd, write_fd);
    pam_conv conv_struct = {conversation_func_fd, &appdata};
    return ::authenticate(&conv_struct, user, service);
}

bool match_prompt(const char* prompt, const std::string& expected_start)
{
    return strncasecmp(prompt, expected_start.c_str(), expected_start.length()) == 0;
}

void add_string(std::string_view str, std::vector<uint8_t>* out)
{
    LengthType len = str.length();
    uint8_t len_bytes[length_size];
    memcpy(len_bytes, &len, length_size);
    out->insert(out->end(), len_bytes, len_bytes + length_size);
    auto str_ptr = reinterpret_cast<const uint8_t*>(str.data());
    out->insert(out->end(), str_ptr, str_ptr + len);
}

std::optional<string> read_string_blocking(int fd)
{
    // All strings read by this function should be short enough so that they are read in one go.
    std::optional<string> rval;
    LengthType len = -1;
    if (read(fd, &len, length_size) == length_size)
    {
        if (len >= 0)
        {
            string message;
            message.resize(len);
            if (read(fd, message.data(), len) == len)
            {
                rval = std::move(message);
            }
        }
    }
    return rval;
}

string gen_auth_tool_run_cmd(Debug debug)
{
    // Get path to current executable. Should typically fit in PATH_MAX.
    string total_path;
    const int len_limit = PATH_MAX + 1;
    char buf[len_limit];
    const char func_call_str[] = "readlink(\"/proc/self/exe\")";
    if (auto len = readlink("/proc/self/exe", buf, len_limit); len > 0)
    {
        if (len < len_limit)
        {
            buf[len] = '\0';
            char* directory = dirname(buf);
            total_path = directory;
            total_path.append("/maxscale_pam_auth_tool");
            if (debug == Debug::YES)
            {
                total_path.append(" -d");
            }
        }
        else
        {
            MXB_ERROR("%s returned too much data.", func_call_str);
        }
    }
    else if (len == 0)
    {
        MXB_ERROR("%s did not return any data.", func_call_str);
    }
    else
    {
        MXB_ERROR("%s failed. Error %i: '%s'", func_call_str, errno, mxb_strerror(errno));
    }
    return total_path;
}

std::vector<uint8_t> create_suid_settings_msg(std::string_view user, std::string_view service)
{
    std::vector<uint8_t> first_msg;
    first_msg.reserve(100);
    mxb::pam::add_string(user, &first_msg);
    mxb::pam::add_string(service, &first_msg);
    return first_msg;
}

std::tuple<int, std::string> next_message(string& msg_buf)
{
    mxb_assert(!msg_buf.empty());

    int rval = -1;
    string rval_msg;

    uint8_t msg_type = msg_buf[0];
    switch (msg_type)
    {
    case mxb::pam::SBOX_CONV:
    case mxb::pam::SBOX_AUTHENTICATED_AS:
    case mxb::pam::SBOX_WARN:
        {
            auto [bytes, message] = extract_string(&msg_buf[1], msg_buf.data() + msg_buf.size());
            if (bytes > 0)
            {
                // The CONV-message should have at least style byte. Username and warning messages are also
                // expected to have contents.
                if (!message.empty())
                {
                    rval = msg_type;
                    rval_msg = std::move(message);
                    msg_buf.erase(0, 1 + bytes);
                }
            }
            else if (bytes == 0)
            {
                // Incomplete message.
                rval = 0;
            }
        }
        break;

    case mxb::pam::SBOX_EOF:
        rval = msg_type;
        // This should be the last message.
        mxb_assert(msg_buf.length() == 1);
        break;

    default:
        break;
    }
    return {rval, rval_msg};
}
}
}

namespace
{
mxb::pam::AuthResult
authenticate(const pam_conv* conv, const mxb::pam::UserData& user, const string& service)
{
    using Result = mxb::pam::AuthResult::Result;
    const char PAM_START_ERR_MSG[] = "Failed to start PAM authentication of user '%s': '%s'.";
    const char PAM_AUTH_ERR_MSG[] = "PAM authentication of user '%s' to service '%s' failed: '%s'.";
    const char PAM_ITEM_ERR_MSG[] = "Failed to fetch mapped username of '%s': '%s'.";
    const char PAM_ACC_ERR_MSG[] = "PAM account check of user '%s' to service '%s' failed: '%s'.";

    auto userc = user.username.c_str();
    auto servicec = service.c_str();

    mxb::pam::AuthResult result;
    bool authenticated = false;
    pam_handle_t* pam_handle = nullptr;

    int pam_status = pam_start(servicec, userc, conv, &pam_handle);
    if (pam_status == PAM_SUCCESS)
    {
        pam_status = pam_authenticate(pam_handle, 0);
        switch (pam_status)
        {
        case PAM_SUCCESS:
            authenticated = true;
            break;

        case PAM_USER_UNKNOWN:
        case PAM_AUTH_ERR:
            // Normal failure, username or password was wrong.
            result.type = Result::WRONG_USER_PW;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, userc, servicec,
                                              pam_strerror(pam_handle, pam_status));
            break;

        default:
            // More exotic error
            result.type = Result::MISC_ERROR;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, userc, servicec,
                                              pam_strerror(pam_handle, pam_status));
            break;
        }
    }
    else
    {
        result.type = Result::MISC_ERROR;
        result.error = mxb::string_printf(PAM_START_ERR_MSG,
                                          userc, pam_strerror(pam_handle, pam_status));
    }

    if (authenticated)
    {
        // Password was correct, check account. Can fail if account expired or was mapped to an unknown
        // username.
        pam_status = pam_acct_mgmt(pam_handle, 0);
        if (pam_status == PAM_SUCCESS)
        {
            const char* user_after_auth = nullptr;
            pam_status = pam_get_item(pam_handle, PAM_USER, (const void**)&user_after_auth);
            if (pam_status == PAM_SUCCESS)
            {
                result.type = Result::SUCCESS;
                if (user_after_auth && strcmp(user_after_auth, userc) != 0)
                {
                    result.mapped_user = user_after_auth;
                }
            }
            else
            {
                result.error = mxb::string_printf(PAM_ITEM_ERR_MSG, userc,
                                                  pam_strerror(pam_handle, pam_status));
            }
        }
        else
        {
            result.type = Result::ACCOUNT_INVALID;
            result.error = mxb::string_printf(PAM_ACC_ERR_MSG, userc, servicec,
                                              pam_strerror(pam_handle, pam_status));
        }
    }
    pam_end(pam_handle, pam_status);
    return result;
}

/**
 * PAM conversation function. The implementation "cheats" by not actually doing
 * I/O with the client. This should only be called once per client when
 * authenticating. See
 * http://www.linux-pam.org/Linux-PAM-html/adg-interface-of-app-expected.html#adg-pam_conv
 * for more information.
 */
int conversation_func(int num_msg, const struct pam_message** messages, struct pam_response** responses_out,
                      void* appdata_ptr)
{
    MXB_DEBUG("Entering PAM conversation function.");
    auto appdata = static_cast<ConversationData*>(appdata_ptr);
    auto mode = appdata->mode;
    auto userdata = appdata->userdata;
    auto pwds = appdata->pwds;
    auto expected_msgs = appdata->exp_msgs;

    const char unexpected_prompt[] = "Unexpected prompt from PAM api when authenticating '%s'. Got '%s', "
                                     "expected '%s'.";
    // The responses are saved as an array of structures. This is unlike the input messages, which is an
    // array of pointers to struct. Each message should have an answer, even if empty.
    auto responses = static_cast<pam_response*>(MXB_CALLOC(num_msg, sizeof(pam_response)));
    if (!responses)
    {
        return PAM_BUF_ERR;
    }

    bool conv_error = false;
    auto userhost = [&userdata]() {
            string rval = userdata->username;
            if (!userdata->remote.empty())
            {
                (rval += '@') += userdata->remote;
            }
            return rval;
        };

    for (int i = 0; i < num_msg; i++)
    {
        const pam_message* message = messages[i];
        pam_response* response = &responses[i];
        int msg_type = message->msg_style;

        auto query_match = [message](const string& expected_start) {
                return mxb::pam::match_prompt(message->msg, expected_start);
            };

        // In an ideal world, these messages would be sent to the client instead of the log. The problem
        // is that the messages should be sent with the AuthSwitchRequest-packet, requiring the blocking
        // PAM api to work with worker-threads. Not worth the trouble unless really required.
        if (msg_type == PAM_ERROR_MSG)
        {
            MXB_WARNING("Error message from PAM api when authenticating '%s': '%s'",
                        userhost().c_str(), message->msg);
        }
        else if (msg_type == PAM_TEXT_INFO)
        {
            MXB_NOTICE("Message from PAM api when authenticating '%s': '%s'",
                       userhost().c_str(), message->msg);
        }
        else if (msg_type == PAM_PROMPT_ECHO_ON || msg_type == PAM_PROMPT_ECHO_OFF)
        {
            if (mode == mxb::pam::AuthMode::PW)
            {
                auto& expected_query = expected_msgs->password_query;
                // PAM system is asking for something. We only know how to answer the expected question,
                // anything else is an error.
                if (expected_query.empty() || query_match(expected_query))
                {
                    response->resp = MXB_STRDUP(pwds->password.c_str());
                    MXB_DEBUG("PAM api asked for '%s'.", message->msg);
                    // retcode should be already 0.
                }
                else
                {
                    MXB_ERROR(unexpected_prompt, userhost().c_str(), message->msg, expected_query.c_str());
                    conv_error = true;
                }
            }
            else
            {
                auto& prompt_ind = appdata->prompt_ind;
                auto& exp_pwq = expected_msgs->password_query;
                auto& exp_2faq = expected_msgs->two_fa_query;
                bool have_exp_pwq = !exp_pwq.empty();
                bool have_exp_2faq = !exp_2faq.empty();
                const string* answer = nullptr;

                // When performing two-factor auth, try to match the expected messages to the
                // pam api messages.
                if (have_exp_pwq && have_exp_2faq)
                {
                    // Match according to expected message.
                    if (query_match(exp_pwq))
                    {
                        answer = &pwds->password;
                    }
                    else if (query_match(exp_2faq))
                    {
                        answer = &pwds->two_fa_code;
                    }
                }
                else if (!have_exp_pwq && !have_exp_2faq)
                {
                    // If no expected messages are given, answer first with password and
                    // then with 2FA.
                    if (prompt_ind == 0)
                    {
                        answer = &pwds->password;
                    }
                    else if (prompt_ind == 1)
                    {
                        answer = &pwds->two_fa_code;
                    }
                }
                else if (have_exp_pwq)
                {
                    // If only expected password query is given, default to 2FA-response.
                    answer = query_match(exp_pwq) ? &pwds->password :  &pwds->two_fa_code;
                }
                else
                {
                    // If only expected 2FA-query is given, default to password-response.
                    answer = query_match(exp_2faq) ? &pwds->two_fa_code : &pwds->password;
                }

                if (answer)
                {
                    response->resp = MXB_STRDUP(answer->c_str());
                    MXB_DEBUG("PAM api asked for '%s'.", message->msg);
                    prompt_ind++;
                }
                else
                {
                    string expected_msgs_str = "none";
                    if ((have_exp_pwq && have_exp_2faq))
                    {
                        expected_msgs_str = mxb::string_printf("'%s' or '%s'",
                                                               exp_pwq.c_str(), exp_2faq.c_str());
                    }
                    MXB_ERROR(unexpected_prompt, userhost().c_str(), message->msg, expected_msgs_str.c_str());
                    conv_error = true;
                }
            }
        }
        else
        {
            // Faulty PAM system or perhaps different api version.
            MXB_ERROR("Unknown PAM message type '%i'.", msg_type);
            conv_error = true;
            mxb_assert(!true);
        }
    }

    if (conv_error)
    {
        // On error, the response output should not be set.
        MXB_FREE(responses);
        return PAM_CONV_ERR;
    }
    else
    {
        *responses_out = responses;
        return PAM_SUCCESS;
    }
}

int conversation_func_fd(int n_msg, const pam_message** messages, pam_response** responses_out,
                         void* appdata_ptr)
{
    auto* responses = static_cast<pam_response*>(MXB_CALLOC(sizeof(pam_response), n_msg));
    if (!responses)
    {
        return PAM_BUF_ERR;
    }

    auto* data = static_cast<ConvDataFd*>(appdata_ptr);
    string& message_buf = data->message_buffer;
    bool conv_error = false;

    for (int i = 0; i < n_msg && !conv_error; i++)
    {
        // Go through the messages, append them to the buffer. On reaching a prompt-type message, send
        // it to the main process and wait for reply.
        const pam_message* msg_info = messages[i];
        if (const char* msg = msg_info->msg; msg)
        {
            if (int msg_len = strlen(msg); msg_len > 0)
            {
                // MariaDB Server limits the total message length to ~10k bytes, do the same. Very long
                // messages would not fit into the pipe anyhow. Such messages should not happen normally.
                // The first byte of the total message is the style byte.
                const int max_buf_size = 10240;
                int max_msg_len = message_buf.empty() ? max_buf_size - 2 :
                    max_buf_size - message_buf.length() - 1;
                int writable = std::min(max_msg_len, msg_len);
                if (writable > 0)
                {
                    if (message_buf.empty())
                    {
                        message_buf.reserve(writable + 2);
                        message_buf.push_back(0);
                    }
                    message_buf.append(msg, writable).push_back('\n');
                }
            }
        }

        auto style = msg_info->msg_style;
        if (style == PAM_PROMPT_ECHO_ON || style == PAM_PROMPT_ECHO_OFF)
        {
            /* Our data ultimately goes to the dialog plugin in the client. The plugin interprets the first
             * byte of the message as the magic number:
             * 2 = echo enabled
             * 4 = echo disabled (like password input)
             */
            uint8_t message_type = (style == PAM_PROMPT_ECHO_ON) ? 2 : 4;
            if (message_buf.empty())
            {
                message_buf.resize(1);
            }
            message_buf[0] = message_type;

            MXB_DEBUG("PAM conv func: sending msg type %i: '%*s'", message_type,
                      (int)message_buf.length() - 1, message_buf.c_str() + 1);

            auto reply = roundtrip(data->read_fd, data->write_fd, message_buf);
            message_buf.clear();

            if (reply)
            {
                MXB_DEBUG("PAM conv func: client replied with '%s'.", reply->c_str());
                // Copy the reply to the response array.
                auto copy = strndup(reply->c_str(), reply->length());
                if (copy)
                {
                    responses[i].resp = copy;
                }
                else
                {
                    conv_error = true;
                }
            }
            else
            {
                conv_error = true;
            }
        }
    }

    if (conv_error)
    {
        // On error, the response output should not be set.
        for (int i = 0; i < n_msg; i++)
        {
            MXB_FREE(responses[i].resp);
        }
        MXB_FREE(responses);
        return PAM_CONV_ERR;
    }
    else
    {
        *responses_out = responses;
        return PAM_SUCCESS;
    }
}

std::optional<string> roundtrip(int fd_in, int fd_out, string_view message)
{
    using namespace mxb::pam;
    /**
     * Format for pam conversation messages:
     * 1 byte - AP_CONV
     * 4 bytes - string length
     * N bytes - string data (message type + contents)
     */
    std::vector<uint8_t> write_buf;
    write_buf.reserve(5 + message.length());
    write_buf.push_back(SBOX_CONV);
    add_string(message, &write_buf);

    std::optional<string> rval;
    if (write(fd_out, write_buf.data(), write_buf.size()) == (int64_t)write_buf.size())
    {
        // Main process should reply with answer from client. This may take a while.
        rval = read_string_blocking(fd_in);
    }
    return rval;
}

/**
 * Extract a length-encoded string from data returned from subprocess.
 *
 * @param ptr Start of data
 * @param end Past-end of data
 * @return Number of bytes consumed and the extracted message. Bytes < 0 on error. Bytes == 0 if a complete
 * message was not available.
 */
std::tuple<int, string> extract_string(const char* ptr, const char* end)
{
    int bytes_read = 0;
    string message;

    LengthType len = -1;
    auto len_size = sizeof(len);
    if (end - ptr >= (ssize_t)len_size)
    {
        memcpy(&len, ptr, len_size);
        if (len == 0)
        {
            bytes_read = len_size;
        }
        else if (len > 0)
        {
            ptr += len_size;
            if (end - ptr >= len)
            {
                message.resize(len);
                memcpy(message.data(), ptr, len);
                bytes_read = len_size + len;
            }
        }
        else
        {
            bytes_read = -1;
        }
    }
    return {bytes_read, message};
}
}
