/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_client_session.hh"

#include <sstream>
#include <security/pam_appl.h>

using maxscale::Buffer;
using std::string;

namespace
{
/**
 * @brief Read the client's password, store it to MySQL-session
 *
 * @param dcb Client DCB
 * @param buffer Buffer containing the password
 *
 * @return True on success, false if memory allocation failed
 */
bool store_client_password(DCB *dcb, GWBUF *buffer)
{
    bool rval = false;
    uint8_t header[MYSQL_HEADER_LEN];

    if (gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, header) == MYSQL_HEADER_LEN)
    {
        size_t plen = gw_mysql_get_byte3(header);
        MYSQL_session *ses = (MYSQL_session*)dcb->data;
        ses->auth_token = (uint8_t *)MXS_CALLOC(plen, sizeof(uint8_t));
        if (ses->auth_token)
        {
            ses->auth_token_len = gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, plen, ses->auth_token);
            rval = true;
        }
    }
    return rval;
}

/**
 * Helper callback for PamClientSession::get_pam_user_services(). See SQLite3
 * documentation for more information.
 *
 * @param data Application data
 * @param columns Number of columns, must be 1
 * @param column_vals Column values
 * @param column_names Column names
 * @return Always 0
 */
int user_services_cb(void *data, int columns, char** column_vals, char** column_names)
{
    ss_dassert(columns == 1);
    PamClientSession::StringVector* results = static_cast<PamClientSession::StringVector*>(data);
    if (column_vals[0])
    {
        results->push_back(column_vals[0]);
    }
    else
    {
        // Empty is a valid value.
        results->push_back("");
    }
    return 0;
}

/** Used by the PAM conversation function */
struct ConversationData
{
    DCB* m_client;
    int m_counter;
    string m_password;

    ConversationData(DCB* client, int counter, const string& password)
        : m_client(client),
          m_counter(counter),
          m_password(password)
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
int conversation_func(int num_msg, const struct pam_message **msg,
                      struct pam_response **resp_out, void *appdata_ptr)
{
    MXS_DEBUG("Entering PAM conversation function.");
    int rval = PAM_CONV_ERR;
    ConversationData* data = static_cast<ConversationData*>(appdata_ptr);
    if (data->m_counter > 1)
    {
        MXS_ERROR("Multiple calls to conversation function for client '%s'. %s",
                  data->m_client->user, GENERAL_ERRMSG);
    }
    else if (num_msg == 1)
    {
        pam_message first = *msg[0];
        if ((first.msg_style != PAM_PROMPT_ECHO_OFF && first.msg_style != PAM_PROMPT_ECHO_ON) ||
            PASSWORD != first.msg)
        {
            MXS_ERROR("Unexpected PAM message: type='%d', contents='%s'",
                      first.msg_style, first.msg);
        }
        else
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
    }
    else
    {
        MXS_ERROR("Conversation function received '%d' messages from API. Only "
                  "singular messages are supported.", num_msg);
    }
    data->m_counter++;
    return rval;
}

/**
 * @brief Check if the client token is valid
 *
 * @param token Client token
 * @param len Length of the token
 * @param output Pointer where the client principal name is stored
 * @return True if client token is valid
 */
bool validate_pam_password(const string& user, const string& password,
                           const string& service, DCB* client)
{
    ConversationData appdata(client, 0, password);
    pam_conv conv_struct = {conversation_func, &appdata};
    bool authenticated = false;
    bool account_ok = false;
    pam_handle_t* pam_handle = NULL;
    int pam_status = pam_start(service.c_str(), user.c_str(), &conv_struct, &pam_handle);
    if (pam_status == PAM_SUCCESS)
    {
        pam_status = pam_authenticate(pam_handle, 0);
        switch (pam_status)
        {
        case PAM_SUCCESS:
            authenticated = true;
            MXS_DEBUG("pam_authenticate returned success.");
            break;
        case PAM_AUTH_ERR:
            MXS_DEBUG("pam_authenticate returned authentication failure"
                      " (wrong password).");
            // Normal failure
            break;
        default:
            MXS_ERROR("pam_authenticate returned error '%d'.", pam_status);
            break;
        }
    }
    else
    {
        MXS_ERROR("Failed to start PAM authentication for user '%s'.", user.c_str());
    }
    if (authenticated)
    {
        pam_status = pam_acct_mgmt(pam_handle, 0);
        account_ok = (pam_status == PAM_SUCCESS);
    }
    pam_end(pam_handle, pam_status);
    return account_ok;
}

}

PamClientSession::PamClientSession(sqlite3* dbhandle, const PamInstance& instance)
    : m_state(PAM_AUTH_INIT),
      m_sequence(0),
      m_dbhandle(dbhandle),
      m_instance(instance)
{
}

PamClientSession:: ~PamClientSession()
{
    sqlite3_close_v2(m_dbhandle);
}

PamClientSession* PamClientSession::create(const PamInstance& inst)
{
    int db_flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_SHAREDCACHE;
    sqlite3* dbhandle = NULL;
    // This handle is only used from one thread
    if (sqlite3_open_v2(inst.m_dbname.c_str(), &dbhandle, db_flags, NULL) == SQLITE_OK)
    {
        sqlite3_busy_timeout(dbhandle, 1000);
    }
    else
    {
        MXS_ERROR("Failed to open SQLite3 handle.");
    }
    PamClientSession* rval = NULL;
    if (!dbhandle || (rval = new (std::nothrow) PamClientSession(dbhandle, inst)) == NULL)
    {
        sqlite3_close_v2(dbhandle);
    }
    return rval;
}

/**
 * @brief Check which PAM services the session user has access to
 *
 * @param auth Authenticator session
 * @param dcb Client DCB
 * @param session MySQL session
 *
 * @return An array of PAM service names for the session user
 */
void PamClientSession::get_pam_user_services(const DCB* dcb, const MYSQL_session* session,
                                             StringVector* services_out)
{
    string services_query = string("SELECT authentication_string FROM ") +
                            m_instance.m_tablename + " WHERE user = '" + session->user +
                            "' AND '" + dcb->remote + "' LIKE host AND (anydb = '1' OR '" +
                            session->db + "' = '' OR '" + session->db +
                            "' LIKE db) ORDER BY authentication_string";
    MXS_DEBUG("PAM services search sql: '%s'.", services_query.c_str());
    char *err;
    /**
     * Try search twice: first time with the current users, second
     * time with fresh users.
     */
    for (int i = 0; i < 2; i++)
    {
        if (i == 0 || service_refresh_users(dcb->service) == 0)
        {
            if (sqlite3_exec(m_dbhandle, services_query.c_str(), user_services_cb,
                         services_out, &err) != SQLITE_OK)
            {
                MXS_ERROR("Failed to execute query: '%s'", err);
                sqlite3_free(err);
            }
            else if (!services_out->empty())
            {
                MXS_DEBUG("User '%s' matched %lu rows in %s db.", session->user,
                          services_out->size(), m_instance.m_tablename.c_str());
                break;
            }
        }
    }
}

/**
 * @brief Create an AuthSwitchRequest packet
 *
 * The server (MaxScale) sends the plugin name "dialog" to the client with the
 * first password prompt. We want to avoid calling the PAM conversation function
 * more than once because it blocks, so we "emulate" its behaviour here.
 * This obviously only works with the basic password authentication scheme.
 *
 * @return Allocated packet
 * @see https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
 */
Buffer PamClientSession::create_auth_change_packet() const
{
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * byte        - Message type
     * string[EOF] - Message
     */
    size_t plen = 1 + DIALOG_SIZE + 1 + PASSWORD.length();
    size_t buflen = MYSQL_HEADER_LEN + plen;
    uint8_t bufdata[buflen];
    uint8_t* pData = bufdata;
    gw_mysql_set_byte3(pData, plen);
    pData += 3;
    *pData++ = m_sequence;
    *pData++ = 0xfe; // AuthSwitchRequest command
    memcpy(pData, DIALOG.c_str(), DIALOG_SIZE); // Plugin name
    pData += DIALOG_SIZE;
    *pData++ = DIALOG_ECHO_DISABLED;
    memcpy(pData, PASSWORD.c_str(), PASSWORD.length()); // First message

    Buffer buffer(bufdata, buflen);
    return buffer;
}

int PamClientSession::authenticate(DCB* dcb)
{
    int rval = ssl_authenticate_check_status(dcb);
    MYSQL_session *ses = static_cast<MYSQL_session*>(dcb->data);
    if (rval == MXS_AUTH_SSL_COMPLETE && *ses->user)
    {
        rval = MXS_AUTH_FAILED;
        if (m_state == PAM_AUTH_INIT)
        {
            /** We need to send the authentication switch packet to change the
             * authentication to something other than the 'mysql_native_password'
             * method */
            Buffer authbuf = create_auth_change_packet();
            if (authbuf.length() && dcb->func.write(dcb, authbuf.release()))
            {
                m_state = PAM_AUTH_DATA_SENT;
                rval = MXS_AUTH_INCOMPLETE;
            }
        }
        else if (m_state == PAM_AUTH_DATA_SENT)
        {
            /** We sent the authentication change packet + plugin name and the client
             * responded with the password. Try to continue authentication without more
             * messages to client. */
            string password((char*)ses->auth_token, ses->auth_token_len);
            StringVector services;
            get_pam_user_services(dcb, ses, &services);

            for (StringVector::const_iterator i = services.begin(); i != services.end(); i++)
            {
                if (validate_pam_password(ses->user, password, *i, dcb))
                {
                    rval = MXS_AUTH_SUCCEEDED;
                    break;
                }
            }
        }
    }
    return rval;
}

int PamClientSession::extract(DCB *dcb, GWBUF *buffer)
{
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &m_sequence);
    m_sequence++;
    int rval = MXS_AUTH_FAILED;

    switch (m_state)
    {
    case PAM_AUTH_INIT:
        // The buffer doesn't have any PAM-specific data yet
        rval = MXS_AUTH_SUCCEEDED;
        break;

    case PAM_AUTH_DATA_SENT:
        store_client_password(dcb, buffer);
        rval = MXS_AUTH_SUCCEEDED;
        break;

    default:
        MXS_ERROR("Unexpected authentication state: %d", m_state);
        ss_dassert(!true);
        break;
    }
    return rval;
}