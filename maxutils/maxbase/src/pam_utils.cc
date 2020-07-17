/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <maxbase/pam_utils.hh>

#include <security/pam_appl.h>
#include <maxbase/alloc.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <strings.h>

using std::string;

namespace
{

const string password_query = "Password: ";

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

    // The responses are saved as an array of structures. This is unlike the input messages, which is an
    // array of pointers to struct. Each message should have an answer, even if empty.
    auto responses = static_cast<pam_response*>(MXS_CALLOC(num_msg, sizeof(pam_response)));
    if (!responses)
    {
        return PAM_BUF_ERR;
    }

    bool conv_error = false;
    string userhost = userdata->remote.empty() ? userdata->username :
        userdata->username + "@" + userdata->remote;
    for (int i = 0; i < num_msg; i++)
    {
        const pam_message* message = messages[i];   // This may crash on Solaris, see PAM documentation.
        pam_response* response = &responses[i];
        int msg_type = message->msg_style;
        // In an ideal world, these messages would be sent to the client instead of the log. The problem
        // is that the messages should be sent with the AuthSwitchRequest-packet, requiring the blocking
        // PAM api to work with worker-threads. Not worth the trouble unless really required.
        if (msg_type == PAM_ERROR_MSG)
        {
            MXB_WARNING("Error message from PAM api when authenticating '%s': '%s'",
                        userhost.c_str(), message->msg);
        }
        else if (msg_type == PAM_TEXT_INFO)
        {
            MXB_NOTICE("Message from PAM api when authenticating '%s': '%s'",
                       userhost.c_str(), message->msg);
        }
        else if (msg_type == PAM_PROMPT_ECHO_ON || msg_type == PAM_PROMPT_ECHO_OFF)
        {
            auto& pwq = expected_msgs->password_query;
            if (mode == mxb::pam::AuthMode::PW)
            {
                // PAM system is asking for something. We only know how to answer the expected question,
                // anything else is an error.
                if (pwq.empty() || (strncasecmp(pwq.c_str(), message->msg, pwq.length()) == 0))
                {
                    response->resp = MXS_STRDUP(pwds->password.c_str());
                    MXB_DEBUG("PAM api asked for '%s'.", message->msg);
                    // retcode should be already 0.
                }
                else
                {
                    MXB_ERROR("Unexpected prompt from PAM api when authenticating '%s'. Got '%s', "
                              "expected '%s'.",
                              userhost.c_str(), message->msg, pwq.c_str());
                    conv_error = true;
                }
            }
            else
            {
                // When performing two-factor auth, try to match the expected messages to the
                // pam api messages. If no expected messages are given, answer first with password and
                // then with 2FA. TODO: add message matching
                if (pwq.empty() && expected_msgs->two_fa_query.empty())
                {
                    auto& prompt_ind = appdata->prompt_ind;
                    if (prompt_ind == 0)
                    {
                        response->resp = MXS_STRDUP(pwds->password.c_str());
                        MXB_DEBUG("PAM api asked for '%s'.", message->msg);
                        prompt_ind++;
                    }
                    else if (prompt_ind == 1)
                    {
                        response->resp = MXS_STRDUP(pwds->two_fa_code.c_str());
                        MXB_DEBUG("PAM api asked for '%s'.", message->msg);
                        prompt_ind++;
                    }
                    else
                    {
                        MXB_ERROR("Unexpected prompt from PAM api when authenticating '%s'. Got '%s', "
                                  "expected none.",
                                  userhost.c_str(), message->msg);
                        conv_error = true;
                    }
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

AuthResult
authenticate(AuthMode mode, const UserData& user, const PwdData& pwds, const std::string& service,
             const ExpectedMsgs& exp_msgs)
{
    const char PAM_START_ERR_MSG[] = "Failed to start PAM authentication of user '%s': '%s'.";
    const char PAM_AUTH_ERR_MSG[] = "PAM authentication of user '%s' to service '%s' failed: '%s'.";
    const char PAM_ACC_ERR_MSG[] = "PAM account check of user '%s' to service '%s' failed: '%s'.";

    ConversationData appdata(mode, &user, &pwds, &exp_msgs);
    pam_conv conv_struct = {conversation_func, &appdata};

    auto userc = user.username.c_str();

    AuthResult result;
    bool authenticated = false;
    pam_handle_t* pam_handle = NULL;

    int pam_status = pam_start(service.c_str(), userc, &conv_struct, &pam_handle);
    if (pam_status == PAM_SUCCESS)
    {
        pam_status = pam_authenticate(pam_handle, 0);
        switch (pam_status)
        {
        case PAM_SUCCESS:
            authenticated = true;
            MXB_DEBUG("pam_authenticate returned success.");
            break;

        case PAM_USER_UNKNOWN:
        case PAM_AUTH_ERR:
            // Normal failure, username or password was wrong.
            result.type = AuthResult::Result::WRONG_USER_PW;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, userc, service.c_str(),
                                              pam_strerror(pam_handle, pam_status));
            break;

        default:
            // More exotic error
            result.type = AuthResult::Result::MISC_ERROR;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, userc, service.c_str(),
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
        pam_status = pam_acct_mgmt(pam_handle, 0);
        switch (pam_status)
        {
        case PAM_SUCCESS:
            result.type = AuthResult::Result::SUCCESS;
            break;

        default:
            // Credentials have already been checked to be ok, so this is a somewhat unexpected error.
            result.type = AuthResult::Result::ACCOUNT_INVALID;
            result.error = mxb::string_printf(PAM_ACC_ERR_MSG, userc, service.c_str(),
                                              pam_strerror(pam_handle, pam_status));
            break;
        }
    }
    pam_end(pam_handle, pam_status);
    return result;
}

AuthResult
authenticate(const std::string& user, const std::string& password, const std::string& client_remote,
             const std::string& service, const std::string& expected_msg)
{
    UserData usr = {user, client_remote};
    PwdData pwds = {password, ""};
    ExpectedMsgs exp_msg = {expected_msg, ""};
    return authenticate(AuthMode::PW, usr, pwds, service, exp_msg);
}

AuthResult authenticate(const string& user, const string& password, const string& service)
{
    UserData usr = {user, ""};
    PwdData pwds = {password, ""};
    ExpectedMsgs exp_msg = {password_query, ""};
    return authenticate(AuthMode::PW, usr, pwds, service, exp_msg);
}
}
}
