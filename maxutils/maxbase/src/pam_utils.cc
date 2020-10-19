/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/pam_utils.hh>

#include <security/pam_appl.h>
#include <maxbase/alloc.h>
#include <maxbase/assert.h>
#include <maxbase/log.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{

/** Used by the PAM conversation function */
class ConversationData
{
public:
    int    m_counter {0};
    string m_client;
    string m_password;
    string m_client_remote; // Client address
    string m_expected_msg;

    ConversationData(const string& client, const string& password, const string& client_remote,
                     const string& expected_msg)
        : m_client(client)
        , m_password(password)
        , m_client_remote(client_remote)
        , m_expected_msg(expected_msg)
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
    ConversationData* data = static_cast<ConversationData*>(appdata_ptr);

    // The responses are saved as an array of structures. This is unlike the input messages, which is an
    // array of pointers to struct. Each message should have an answer, even if empty.
    auto responses = static_cast<pam_response*>(MXS_CALLOC(num_msg, sizeof(pam_response)));
    if (!responses)
    {
        return PAM_BUF_ERR;
    }

    bool conv_error = false;
    string userhost = data->m_client_remote.empty() ? data->m_client :
            data->m_client + "@" + data->m_client_remote;
    for (int i = 0; i < num_msg; i++)
    {
        const pam_message* message = messages[i]; // This may crash on Solaris, see PAM documentation.
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
            auto exp = data->m_expected_msg;
            // PAM system is asking for something. We only know how to answer the expected question,
            // anything else is an error.
            if (data->m_expected_msg.empty() || message->msg == data->m_expected_msg)
            {
                response->resp = MXS_STRDUP(data->m_password.c_str());
                // retcode should be already 0.
            }
            else
            {
                MXB_ERROR("Unexpected prompt from PAM api when authenticating '%s': '%s'. "
                          "Only '%s' is allowed.",
                          userhost.c_str(), message->msg, data->m_expected_msg.c_str());
                conv_error = true;
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

    data->m_counter++;
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

PamResult
pam_authenticate(const std::string& user, const std::string& password, const std::string& client_remote,
                 const std::string& service, const std::string& expected_msg)
{
    const char PAM_START_ERR_MSG[] = "Failed to start PAM authentication of user '%s': '%s'.";
    const char PAM_AUTH_ERR_MSG[] = "PAM authentication of user '%s' to service '%s' failed: '%s'.";
    const char PAM_ACC_ERR_MSG[] = "PAM account check of user '%s' to service '%s' failed: '%s'.";

    ConversationData appdata(user, password, client_remote, expected_msg);
    pam_conv conv_struct = {conversation_func, &appdata};

    PamResult result;
    bool authenticated = false;
    pam_handle_t* pam_handle = NULL;

    int pam_status = pam_start(service.c_str(), user.c_str(), &conv_struct, &pam_handle);
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
            result.type = PamResult::Result::WRONG_USER_PW;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, user.c_str(), service.c_str(),
                                              pam_strerror(pam_handle, pam_status));
            break;

        default:
            // More exotic error
            result.type = PamResult::Result::MISC_ERROR;
            result.error = mxb::string_printf(PAM_AUTH_ERR_MSG, user.c_str(), service.c_str(),
                                              pam_strerror(pam_handle, pam_status));
            break;
        }
    }
    else
    {
        result.type = PamResult::Result::MISC_ERROR;
        result.error = mxb::string_printf(PAM_START_ERR_MSG,
                                          user.c_str(), pam_strerror(pam_handle, pam_status));
    }

    if (authenticated)
    {
        pam_status = pam_acct_mgmt(pam_handle, 0);
        switch (pam_status)
        {
        case PAM_SUCCESS:
            result.type = PamResult::Result::SUCCESS;
            break;

        default:
            // Credentials have already been checked to be ok, so this is a somewhat unexpected error.
            result.type = PamResult::Result::ACCOUNT_INVALID;
            result.error = mxb::string_printf(PAM_ACC_ERR_MSG, user.c_str(), service.c_str(),
                                              pam_strerror(pam_handle, pam_status));
            break;
        }
    }
    pam_end(pam_handle, pam_status);
    return result;
}

PamResult pam_authenticate(const string& user, const string& password, const string& service,
                           const string& expected_msg)
{
    return pam_authenticate(user, password, "", service, expected_msg);
}
}
