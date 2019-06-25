/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_client_session.hh"

#include <sstream>
#include <security/pam_appl.h>
#include <maxscale/event.hh>

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
bool store_client_password(DCB* dcb, GWBUF* buffer)
{
    bool rval = false;
    uint8_t header[MYSQL_HEADER_LEN];

    if (gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, header) == MYSQL_HEADER_LEN)
    {
        size_t plen = gw_mysql_get_byte3(header);
        MYSQL_session* ses = (MYSQL_session*)dcb->data;
        ses->auth_token = (uint8_t*)MXS_CALLOC(plen, sizeof(uint8_t));
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
int user_services_cb(void* data, int columns, char** column_vals, char** column_names)
{
    mxb_assert(columns == 1);
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
    int    m_calls {0};     // How many times the conversation function has been called?
    string m_client;        // Client username
    string m_password;      // Password to give to first password prompt
    string m_client_remote; // Client address

    ConversationData(const string& client, const string& password, const string& client_remote)
        : m_client(client)
        , m_password(password)
        , m_client_remote(client_remote)
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
    MXS_DEBUG("Entering PAM conversation function.");
    ConversationData* data = static_cast<ConversationData*>(appdata_ptr);

    // The responses are saved as an array of structures. This is unlike the input messages, which is an
    // array of pointers to struct. Each message should have an answer, even if empty.
    auto responses = static_cast<pam_response*>(MXS_CALLOC(num_msg, sizeof(pam_response)));
    if (!responses)
    {
        return PAM_BUF_ERR;
    }

    bool conv_error = false;
    string userhost = data->m_client + "@" + data->m_client_remote;
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
            // PAM system is asking for something. We only know how to answer the password question,
            // anything else is an error.
            if (message->msg == PASSWORD)
            {
                response->resp = MXS_STRDUP(data->m_password.c_str());
                // retcode should be already 0.
            }
            else
            {
                MXB_ERROR("Unexpected prompt from PAM api when authenticating '%s': '%s'. "
                          "Only '%s' is allowed.", userhost.c_str(), message->msg, PASSWORD.c_str());
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

    data->m_calls++;
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

/**
 * @brief Check if the client password is correct for the service
 *
 * @param user Username
 * @param password Password
 * @param service Which PAM service is the user logging to
 * @param client_remote Client address. Used for log messages.
 * @return True if username & password are ok and account is valid.
 */
bool validate_pam_password(const string& user, const string& password, const string& service,
                           const string& client_remote)
{
    const char PAM_START_ERR_MSG[] = "Failed to start PAM authentication for user '%s': '%s'.";
    const char PAM_AUTH_ERR_MSG[] = "Pam authentication for user '%s' failed: '%s'.";
    const char PAM_ACC_ERR_MSG[] = "Pam account check for user '%s' failed: '%s'.";
    ConversationData appdata(user, password, client_remote);
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

        case PAM_USER_UNKNOWN:
        case PAM_AUTH_ERR:
            // Normal failure, username or password was wrong.
            MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                          PAM_AUTH_ERR_MSG,
                          user.c_str(),
                          pam_strerror(pam_handle, pam_status));
            break;

        default:
            // More exotic error
            MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                          PAM_AUTH_ERR_MSG,
                          user.c_str(),
                          pam_strerror(pam_handle, pam_status));
            break;
        }
    }
    else
    {
        MXS_ERROR(PAM_START_ERR_MSG, user.c_str(), pam_strerror(pam_handle, pam_status));
    }

    if (authenticated)
    {
        pam_status = pam_acct_mgmt(pam_handle, 0);
        switch (pam_status)
        {
        case PAM_SUCCESS:
            account_ok = true;
            break;

        default:
            // Credentials have already been checked to be ok, so this is again a bit of an exotic error.
            MXS_ERROR(PAM_ACC_ERR_MSG, user.c_str(), pam_strerror(pam_handle, pam_status));
            break;
        }
    }
    pam_end(pam_handle, pam_status);
    return account_ok;
}
}

PamClientSession::PamClientSession(sqlite3* dbhandle, const PamInstance& instance)
    : m_dbhandle(dbhandle)
    , m_instance(instance)
{
}

PamClientSession::~PamClientSession()
{
    sqlite3_close_v2(m_dbhandle);
}

PamClientSession* PamClientSession::create(const PamInstance& inst)
{
    // This handle is only used from one thread, can define no_mutex.
    sqlite3* dbhandle = NULL;
    bool error = false;
    int db_flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_SHAREDCACHE | SQLITE_OPEN_NOMUTEX;
    const char* filename = inst.m_dbname.c_str();
    if (sqlite3_open_v2(filename, &dbhandle, db_flags, NULL) == SQLITE_OK)
    {
        sqlite3_busy_timeout(dbhandle, 1000);
    }
    else
    {
        if (dbhandle)
        {
            MXS_ERROR(SQLITE_OPEN_FAIL, filename, sqlite3_errmsg(dbhandle));
        }
        else
        {
            // This means memory allocation failed.
            MXS_ERROR(SQLITE_OPEN_OOM, filename);
        }
        error = true;
    }

    PamClientSession* rval = NULL;
    if (!error && ((rval = new(std::nothrow) PamClientSession(dbhandle, inst)) == NULL))
    {
        error = true;
    }

    if (error)
    {
        sqlite3_close_v2(dbhandle);
    }
    return rval;
}

/**
 * Check which PAM services the session user has access to.
 *
 * @param dcb Client DCB
 * @param session MySQL session
 * @param services_out Output for services
 */
void PamClientSession::get_pam_user_services(const DCB* dcb, const MYSQL_session* session,
                                             StringVector* services_out)
{
    string services_query = string("SELECT authentication_string FROM ") + m_instance.m_tablename + " WHERE "
        + FIELD_USER + " = '" + session->user + "'"
        + " AND '" + dcb->remote + "' LIKE " + FIELD_HOST
        + " AND (" + FIELD_ANYDB + " = '1' OR '" + session->db + "' IN ('information_schema', '') OR '"
        + session->db + "' LIKE " + FIELD_DB + ")"
        + " AND " + FIELD_PROXY + " = '0' ORDER BY authentication_string;";
    MXS_DEBUG("PAM services search sql: '%s'.", services_query.c_str());

    char* err;
    if (sqlite3_exec(m_dbhandle, services_query.c_str(), user_services_cb, services_out, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to execute query: '%s'", err);
        sqlite3_free(err);
    }

    auto word_entry = [](size_t num) -> const char* {
        return (num == 1) ? "entry" : "entries";
    };

    if (!services_out->empty())
    {
        auto num_services = services_out->size();
        MXS_INFO("Found %lu valid PAM user %s for '%s'@'%s'.",
                 num_services, word_entry(num_services), session->user, dcb->remote);
    }
    else
    {
        // No service found for user with correct username & host.
        // Check if a matching anonymous user exists.
        const string anon_query = string("SELECT authentication_string FROM ") + m_instance.m_tablename
            + " WHERE " + FIELD_USER + " = ''"
            + " AND '" + dcb->remote + "' LIKE " + FIELD_HOST +
            + " AND " + FIELD_PROXY + " = '1' ORDER BY authentication_string;";
        MXS_DEBUG("PAM proxy user services search sql: '%s'.", anon_query.c_str());

        if (sqlite3_exec(m_dbhandle, anon_query.c_str(), user_services_cb, services_out, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to execute query: '%s'", err);
            sqlite3_free(err);
        }
        else
        {
            auto num_services = services_out->size();
            if (num_services == 0)
            {
                MXS_INFO("Found no PAM user entries for '%s'@'%s'.", session->user, dcb->remote);
            }
            else
            {
                MXS_INFO("Found %lu matching anonymous PAM user %s for '%s'@'%s'.",
                         num_services, word_entry(num_services), session->user, dcb->remote);
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
 * @see
 * https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
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
    *pData++ = MYSQL_REPLY_AUTHSWITCHREQUEST;
    memcpy(pData, DIALOG.c_str(), DIALOG_SIZE); // Plugin name
    pData += DIALOG_SIZE;
    *pData++ = DIALOG_ECHO_DISABLED;
    memcpy(pData, PASSWORD.c_str(), PASSWORD.length());     // First message

    Buffer buffer(bufdata, buflen);
    return buffer;
}

int PamClientSession::authenticate(DCB* dcb)
{
    int rval = MXS_AUTH_SSL_COMPLETE;
    MYSQL_session* ses = static_cast<MYSQL_session*>(dcb->data);
    if (*ses->user)
    {
        rval = MXS_AUTH_FAILED;
        if (m_state == State::INIT)
        {
            /** We need to send the authentication switch packet to change the
             * authentication to something other than the 'mysql_native_password'
             * method */
            Buffer authbuf = create_auth_change_packet();
            if (authbuf.length() && dcb->func.write(dcb, authbuf.release()))
            {
                m_state = State::ASKED_FOR_PW;
                rval = MXS_AUTH_INCOMPLETE;
            }
        }
        else if (m_state == State::PW_RECEIVED)
        {
            /** We sent the authentication change packet + plugin name and the client
             * responded with the password. Try to continue authentication without more
             * messages to client. */
            string password((char*)ses->auth_token, ses->auth_token_len);
            /*
             * Authentication may be attempted twice: first with old user account info and then with
             * updated info. Updating may fail if it has been attempted too often lately. The second password
             * check is useless if the user services are same as on the first attempt.
             */
            bool authenticated = false;
            StringVector services_old;
            for (int loop = 0; loop < 2 && !authenticated; loop++)
            {
                if (loop == 0 || service_refresh_users(dcb->service) == 0)
                {
                    bool try_validate = true;
                    StringVector services;
                    get_pam_user_services(dcb, ses, &services);
                    if (loop == 0)
                    {
                        services_old = services;
                    }
                    else if (services == services_old)
                    {
                        try_validate = false;
                    }
                    if (try_validate)
                    {
                        for (StringVector::iterator iter = services.begin();
                             iter != services.end() && !authenticated;
                             iter++)
                        {
                            // The server PAM plugin uses "mysql" as the default service when authenticating
                            // a user with no service.
                            if (iter->empty())
                            {
                                *iter = "mysql";
                            }
                            if (validate_pam_password(ses->user, password, *iter, dcb->remote))
                            {
                                authenticated = true;
                            }
                        }
                    }
                }
            }
            if (authenticated)
            {
                rval = MXS_AUTH_SUCCEEDED;
            }
            m_state = State::DONE;
        }
    }
    return rval;
}

bool PamClientSession::extract(DCB* dcb, GWBUF* buffer)
{
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &m_sequence);
    m_sequence++;
    bool rval = false;

    switch (m_state)
    {
        case State::INIT:
        // The buffer doesn't have any PAM-specific data yet, as it's the normal HandShakeResponse.
        rval = true;
        break;

        case State::ASKED_FOR_PW:
        // Client should have responses with password.
        if (store_client_password(dcb, buffer))
        {
            m_state = State::PW_RECEIVED;
            rval = true;
        }
        break;

    default:
        MXS_ERROR("Unexpected authentication state: %d", static_cast<int>(m_state));
        mxb_assert(!true);
        break;
    }
    return rval;
}
