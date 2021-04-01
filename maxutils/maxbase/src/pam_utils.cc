/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/pam_utils.hh>

#include <security/pam_appl.h>
#include <maxbase/alloc.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <strings.h>

using std::string;

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
    auto responses = static_cast<pam_response*>(MXS_CALLOC(num_msg, sizeof(pam_response)));
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
                    response->resp = MXS_STRDUP(pwds->password.c_str());
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
                    response->resp = MXS_STRDUP(answer->c_str());
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
        MXS_FREE(responses);
        return PAM_CONV_ERR;
    }
    else
    {
        *responses_out = responses;
        return PAM_SUCCESS;
    }
}
}

namespace maxbase
{
namespace pam
{
const std::string EXP_PW_QUERY = "Password";

AuthResult
authenticate(AuthMode mode, const UserData& user, const PwdData& pwds, const AuthSettings& sett,
             const ExpectedMsgs& exp_msgs)
{
    const char PAM_START_ERR_MSG[] = "Failed to start PAM authentication of user '%s': '%s'.";
    const char PAM_AUTH_ERR_MSG[] = "PAM authentication of user '%s' to service '%s' failed: '%s'.";
    const char PAM_ITEM_ERR_MSG[] = "Failed to fetch mapped username of '%s': '%s'.";
    const char PAM_ACC_ERR_MSG[] = "PAM account check of user '%s' to service '%s' failed: '%s'.";

    ConversationData appdata(mode, &user, &pwds, &exp_msgs);
    pam_conv conv_struct = {conversation_func, &appdata};

    auto userc = user.username.c_str();
    auto servicec = sett.service.c_str();

    AuthResult result;
    bool authenticated = false;
    pam_handle_t* pam_handle = nullptr;

    int pam_status = pam_start(servicec, userc, &conv_struct, &pam_handle);
    if (pam_status == PAM_SUCCESS)
    {
        pam_status = pam_authenticate(pam_handle, 0);
        switch (pam_status)
        {
        case PAM_SUCCESS:
            authenticated = true;
            MXB_DEBUG("pam_authenticate returned success.");
            if (sett.mapping_on)
            {
                // Fetch the final username. It may not be identical to the one doing the authentication.
                const void* user_after_auth = nullptr;
                int rc = pam_get_item(pam_handle, PAM_USER, &user_after_auth);
                if (rc == PAM_SUCCESS)
                {
                    if (user_after_auth)
                    {
                        result.mapped_user = static_cast<const char*>(user_after_auth);
                    }
                }
                else
                {
                    MXB_WARNING(PAM_ITEM_ERR_MSG, userc, pam_strerror(pam_handle, rc));
                }
            }
            break;

        case PAM_USER_UNKNOWN:
        case PAM_AUTH_ERR:
            // Normal failure, username or password was wrong.
            result.type = AuthResult::Result::WRONG_USER_PW;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, userc, servicec,
                                              pam_strerror(pam_handle, pam_status));
            break;

        default:
            // More exotic error
            result.type = AuthResult::Result::MISC_ERROR;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, userc, servicec,
                                              pam_strerror(pam_handle, pam_status));
            break;
        }
    }
    else
    {
        result.type = AuthResult::Result::MISC_ERROR;
        result.error = mxb::string_printf(PAM_START_ERR_MSG,
                                          userc, pam_strerror(pam_handle, pam_status));
    }

    if (authenticated)
    {
        if (sett.mapping_on)
        {
            // Don't check account, since username may have changed.
            result.type = AuthResult::Result::SUCCESS;
        }
        else
        {
            pam_status = pam_acct_mgmt(pam_handle, 0);
            if (pam_status == PAM_SUCCESS)
            {
                result.type = AuthResult::Result::SUCCESS;
            }
            else
            {
                // Credentials have already been checked to be ok, so this is a somewhat unexpected error.
                result.type = AuthResult::Result::ACCOUNT_INVALID;
                result.error = mxb::string_printf(PAM_ACC_ERR_MSG, userc, servicec,
                                                  pam_strerror(pam_handle, pam_status));
            }
        }
    }
    pam_end(pam_handle, pam_status);
    return result;
}

AuthResult authenticate(const string& user, const string& password, const string& service)
{
    UserData usr = {user, ""};
    PwdData pwds = {password, ""};
    AuthSettings sett = {service, false};
    ExpectedMsgs exp_msg = {EXP_PW_QUERY, ""};
    return authenticate(AuthMode::PW, usr, pwds, sett, exp_msg);
}

bool match_prompt(const char* prompt, const std::string& expected_start)
{
    return strncasecmp(prompt, expected_start.c_str(), expected_start.length()) == 0;
}
}
}
