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
#include <maxbase/pam_utils.hh>
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
}

PamClientSession::PamClientSession(sqlite3* dbhandle, const PamInstance& instance)
    : m_state(PAM_AUTH_INIT)
    , m_sequence(0)
    , m_dbhandle(dbhandle)
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
    int db_flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_SHAREDCACHE | SQLITE_OPEN_NOMUTEX
        | SQLITE_OPEN_URI;
    if (sqlite3_open_v2(inst.m_dbname.c_str(), &dbhandle, db_flags, NULL) == SQLITE_OK)
    {
        sqlite3_busy_timeout(dbhandle, 1000);
    }
    else
    {
        MXS_ERROR("Failed to open SQLite3 handle.");
    }
    PamClientSession* rval = NULL;
    if (!dbhandle || (rval = new(std::nothrow) PamClientSession(dbhandle, inst)) == NULL)
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
        + " AND (" + FIELD_ANYDB + " = '1' OR '" + session->db + "' = '' OR '"
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
            + " AND '" + dcb->remote + "' LIKE " + FIELD_HOST
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
    *pData++ = 0xfe;                            // AuthSwitchRequest command
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
                        for (auto iter = services.begin(); iter != services.end() && !authenticated; ++iter)
                        {
                            string service = *iter;
                            // The server PAM plugin uses "mysql" as the default service when authenticating
                            // a user with no service.
                            if (service.empty())
                            {
                                service = "mysql";
                            }

                            mxb::PamResult res = mxb::pam_authenticate(ses->user, password, service,
                                                                       PASSWORD);
                            if (res.type == mxb::PamResult::Result::SUCCESS)
                            {
                                authenticated = true;
                            }
                            else
                            {
                                MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE, "%s",
                                              res.error.c_str());
                            }
                        }
                    }
                }
            }
            if (authenticated)
            {
                rval = MXS_AUTH_SUCCEEDED;
            }
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
    case PAM_AUTH_INIT:
        // The buffer doesn't have any PAM-specific data yet
        rval = true;
        break;

    case PAM_AUTH_DATA_SENT:
        if (store_client_password(dcb, buffer))
        {
            rval = true;
        }
        break;

    default:
        MXS_ERROR("Unexpected authentication state: %d", m_state);
        mxb_assert(!true);
        break;
    }
    return rval;
}
