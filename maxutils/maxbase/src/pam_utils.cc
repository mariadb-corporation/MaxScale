/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/pam_utils.hh>

#include <security/pam_appl.h>
#include <maxbase/alloc.h>
#include <maxbase/log.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{

const char GENERAL_ERRMSG[] = "Only simple password-based PAM authentication with one call "
                              "to the conversation function is supported.";

/** Used by the PAM conversation function */
class ConversationData
{
public:
    string m_client;
    string m_password;
    int m_counter {0};
    string m_expected_msg;

    ConversationData(const string& client, const string& password, const string& expected_msg)
        : m_client(client)
        , m_password(password)
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
int conversation_func(int num_msg,
                      const struct pam_message** msg,
                      struct pam_response** resp_out,
                      void* appdata_ptr)
{
    MXB_DEBUG("Entering PAM conversation function.");
    int rval = PAM_CONV_ERR;
    ConversationData* data = static_cast<ConversationData*>(appdata_ptr);
    if (data->m_counter > 1)
    {
        MXB_ERROR("Multiple calls to conversation function for client '%s'. %s",
                  data->m_client.c_str(), GENERAL_ERRMSG);
    }
    else if (num_msg == 1)
    {
        pam_message first = *msg[0];
        // Check that the first message from the PAM system is as expected.
        if ((first.msg_style == PAM_PROMPT_ECHO_OFF || first.msg_style == PAM_PROMPT_ECHO_ON)
            && (data->m_expected_msg.empty() || data->m_expected_msg == first.msg))
        {
            pam_response* response = static_cast<pam_response*>(MXS_MALLOC(sizeof(pam_response)));
            if (response)
            {
                response->resp_retcode = 0;
                response->resp = MXS_STRDUP(data->m_password.c_str());
                *resp_out = response;
                rval = PAM_SUCCESS;
            }
        }
        else
        {
            MXB_ERROR("Unexpected PAM message: type='%d', contents='%s'", first.msg_style, first.msg);
        }
    }
    else
    {
        MXB_ERROR("Conversation function received '%d' messages from API. Only singular messages are "
                  "supported.", num_msg);
    }
    data->m_counter++;
    return rval;
}

}

namespace maxbase
{

PamResult pam_authenticate(const string& user, const string& password, const string& service,
                           const string& expected_msg)
{
    const char PAM_START_ERR_MSG[] = "Failed to start PAM authentication for user '%s': '%s'.";
    const char PAM_AUTH_ERR_MSG[] = "PAM authentication for user '%s' failed: '%s'.";
    const char PAM_ACC_ERR_MSG[] = "PAM account check for user '%s' failed: '%s'.";

    ConversationData appdata(user, password, expected_msg);
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
                result.error = mxb::string_printf(PAM_AUTH_ERR_MSG,
                                                  user.c_str(), pam_strerror(pam_handle, pam_status));
                break;

            default:
                // More exotic error
                result.type = PamResult::Result::MISC_ERROR;
                result.error = mxb::string_printf(PAM_AUTH_ERR_MSG,
                                                  user.c_str(), pam_strerror(pam_handle, pam_status));
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
                result.error = mxb::string_printf(PAM_ACC_ERR_MSG,
                                                  user.c_str(), pam_strerror(pam_handle, pam_status));
                break;
        }
    }
    pam_end(pam_handle, pam_status);
    return result;
}

}
