/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_client_session.hh"

#include <set>
#include <maxbase/pam_utils.hh>
#include <maxbase/format.hh>
#include <maxscale/event.hh>

using maxscale::Buffer;
using std::string;

using SSQLite = SQLite::SSQLite;

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

struct UserData
{
    string host;
    string authentication_string;
    string default_role;
    bool anydb {false};

    static bool compare(const UserData& lhs, const UserData& rhs)
    {
        // Order entries according to https://mariadb.com/kb/en/library/create-user/
        const string& lhost = lhs.host;
        const string& rhost = rhs.host;
        const char wildcards[] = "%_";
        auto lwc_pos = lhost.find_first_of(wildcards);
        auto rwc_pos = rhost.find_first_of(wildcards);
        bool lwc = (lwc_pos != string::npos);
        bool rwc = (rwc_pos != string::npos);

        // The host without wc:s sorts earlier than the one with them. If both have wc:s, the one with the
        // later wc wins. If neither have wildcards, use string order. This should be rare.
        return ((!lwc && rwc) || (lwc && rwc && lwc_pos > rwc_pos) || (!lwc && !rwc && lhost < rhost));
    }

};

using UserDataArr = std::vector<UserData>;

int user_data_cb(UserDataArr* data, int columns, char** column_vals, char** column_names)
{
    mxb_assert(columns == 4);
    UserData new_row;
    new_row.host = column_vals[0];
    new_row.authentication_string = column_vals[1];
    new_row.default_role = column_vals[2];
    new_row.anydb = (column_vals[3][0] == '1');
    data->push_back(new_row);
    return 0;
}

int anon_user_data_cb(UserDataArr* data, int columns, char** column_vals, char** column_names)
{
    mxb_assert(columns == 2);
    UserData new_row;
    new_row.host = column_vals[0];
    new_row.authentication_string = column_vals[1];
    data->push_back(new_row);
    return 0;
}

int string_cb(PamClientSession::StringVector* data, int columns, char** column_vals, char** column_names)
{
    mxb_assert(columns == 1);
    if (column_vals[0])
    {
        data->push_back(column_vals[0]);
    }
    else
    {
        // Empty is a valid value.
        data->push_back("");
    }
    return 0;
}

int row_count_cb(int* data, int columns, char** column_vals, char** column_names)
{
    (*data)++;
    return 0;
}

}

PamClientSession::PamClientSession(const PamInstance& instance, SSQLite sqlite)
    : m_instance(instance)
    , m_sqlite(std::move(sqlite))
{
}

PamClientSession* PamClientSession::create(const PamInstance& inst)
{
    PamClientSession* rval = nullptr;
    // This handle is only used from one thread, can define no_mutex.
    int db_flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_SHAREDCACHE | SQLITE_OPEN_NOMUTEX;
    string sqlite_error;
    auto sqlite = SQLite::create(inst.m_dbname, db_flags, &sqlite_error);
    if (sqlite)
    {
        sqlite->set_timeout(1000);
        rval = new(std::nothrow) PamClientSession(inst, std::move(sqlite));
    }
    else
    {
        MXB_ERROR("Could not create PAM authenticator session: %s", sqlite_error.c_str());
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
    const char* user = session->user;
    const char* host = dcb->remote;
    const string db = session->db;
    // First search for a normal matching user.
    const string columns = FIELD_HOST + ", " + FIELD_AUTHSTR + ", " + FIELD_DEF_ROLE + ", " + FIELD_ANYDB;
    const string filter = "('%s' LIKE " + FIELD_HOST + ") AND (" + FIELD_IS_ROLE + " = 0)";
    const string users_filter = "(" + FIELD_USER + " = '%s') AND " + filter;
    const string users_query_fmt = "SELECT " + columns + " FROM " + TABLE_USER + " WHERE "
            + users_filter + ";";

    string users_query = mxb::string_printf(users_query_fmt.c_str(), user, host);
    UserDataArr matching_users;
    m_sqlite->exec(users_query, user_data_cb, &matching_users);

    if (!matching_users.empty())
    {
        // Only consider the best matching userdata.
        auto best_entry = *std::min_element(matching_users.begin(), matching_users.end(), UserData::compare);

        // Accept the user if the entry has a direct global privilege or if the user is not
        // connecting to a specific database.
        if (best_entry.anydb || db.empty()
            // Check db-specific access.
            || (user_can_access_db(user, best_entry.host, db))
            // Check role-based access.
            || (!best_entry.default_role.empty() && role_can_access_db(best_entry.default_role, db)))
        {
            MXS_INFO("Found matching PAM user '%s'@'%s' for client '%s'@'%s' with sufficient privileges.",
                     user, best_entry.host.c_str(), user, host);
            services_out->push_back(best_entry.authentication_string);
        }
        else
        {
            MXS_INFO("Found matching PAM user '%s'@'%s' for client '%s'@'%s' but user does not have "
                     "sufficient privileges.", user, best_entry.host.c_str(), user, host);
        }
    }
    else
    {
        // No normal user entry found for the username.
        // Check if a matching anonymous user exists. Privileges are not checked for anonymous users since
        // the authenticator does not know the final mapped user. Roles are also not supported.
        const string anon_columns = FIELD_HOST + ", " + FIELD_AUTHSTR;
        const string anon_filter = "(" + FIELD_USER + " = '') AND " + filter + " AND ("
                                   + FIELD_HAS_PROXY + " = '1')";
        const string anon_query_fmt = "SELECT " + anon_columns + " FROM " + TABLE_USER
                                      + " WHERE " + anon_filter + ";";
        string anon_query = mxb::string_printf(anon_query_fmt.c_str(), host);
        MXS_DEBUG("PAM proxy user services search sql: '%s'.", anon_query.c_str());

        UserDataArr anon_entries;
        m_sqlite->exec(anon_query, anon_user_data_cb, &anon_entries);
        if (anon_entries.empty())
        {
            MXB_INFO("Found no matching PAM user for client '%s'@'%s'.", user, host);
        }
        else
        {
            auto best_entry = *std::min_element(anon_entries.begin(), anon_entries.end(), UserData::compare);
            MXB_INFO("Found matching anonymous PAM user ''@'%s' for client '%s'@'%s'.",
                     best_entry.host.c_str(), user, host);
            services_out->push_back(best_entry.authentication_string);
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
                        for (auto iter = services.begin(); iter != services.end() && !authenticated; ++iter)
                        {
                            string service = *iter;
                            // The server PAM plugin uses "mysql" as the default service when authenticating
                            // a user with no service.
                            if (service.empty())
                            {
                                service = "mysql";
                            }

                            mxb::PamResult res = mxb::pam_authenticate(ses->user, password, dcb->remote,
                                                                       service, PASSWORD);
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

bool PamClientSession::role_can_access_db(const std::string& role, const std::string& target_db)
{
    // Roles are tricky since one role may have access to other roles and so on. May need to perform
    // multiple queries.
    using StringSet = std::set<string>;
    StringSet open_set; // roles which still need to be expanded.
    StringSet closed_set; // roles which have been checked already.

    const string role_anydb_query_fmt = "SELECT 1 FROM " + TABLE_USER + " WHERE ("
            + FIELD_USER +" = '%s' AND " + FIELD_ANYDB + " = 1 AND " + FIELD_IS_ROLE + " = 1);";
    const string role_map_query_fmt = "SELECT " + FIELD_ROLE + " FROM " + TABLE_ROLES_MAPPING + " WHERE ("
            + FIELD_USER +" = '%s' AND " + FIELD_HOST + " = '');";

    open_set.insert(role);
    bool privilege_found = false;
    while (!open_set.empty() && !privilege_found)
    {
        string current_role = *open_set.begin();
        // First, check if role has global privilege.
        int count = 0;
        string role_anydb_query = mxb::string_printf(role_anydb_query_fmt.c_str(), current_role.c_str());
        m_sqlite->exec(role_anydb_query.c_str(), row_count_cb, &count);
        if (count > 0)
        {
            privilege_found = true;
        }
        // No global privilege, check db-level privilege.
        else if (user_can_access_db(current_role, "", target_db))
        {
            privilege_found = true;
        }
        else
        {
            // The current role does not have access to db. Add linked roles to the open set.
            string role_map_query = mxb::string_printf(role_map_query_fmt.c_str(), current_role.c_str());
            StringVector linked_roles;
            m_sqlite->exec(role_map_query, string_cb, &linked_roles);
            for (const auto& linked_role : linked_roles)
            {
                if (open_set.count(linked_role) == 0 && closed_set.count(linked_role) == 0)
                {
                    open_set.insert(linked_role);
                }
            }
        }

        open_set.erase(current_role);
        closed_set.insert(current_role);
    }
    return privilege_found;
}

bool PamClientSession::user_can_access_db(const std::string& user, const std::string& host,
                                          const std::string& target_db)
{
    const string sql_fmt = "SELECT 1 FROM " + TABLE_DB
            + " WHERE (user = '%s' AND host = '%s' AND db = '%s');";
    string sql = mxb::string_printf(sql_fmt.c_str(), user.c_str(), host.c_str(), target_db.c_str());
    int result = 0;
    m_sqlite->exec(sql, row_count_cb, &result);
    return result > 0;
}
