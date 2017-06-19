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

#define MXS_MODULE_NAME "PAMAuth"

#include "../pam_auth.hh"

#include <new>
#include <string>
#include <sstream>
#include <vector>
#include <security/pam_appl.h>
#include <maxscale/alloc.h>
#include <maxscale/authenticator.h>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/secrets.h>
#include <maxscale/sqlite3.h>

using std::string;
using maxscale::Buffer;

#define PAM_USERS_QUERY_NUM_FIELDS 5
/** Name of the in-memory database */
#define PAM_DATABASE_NAME "file:pam.db?mode=memory&cache=shared"
/** The table name where we store the users */
#define PAM_TABLE_NAME    "pam_users"

/** Flags for sqlite3_open_v2() */
static int db_flags = SQLITE_OPEN_READWRITE |
                      SQLITE_OPEN_CREATE |
                      SQLITE_OPEN_URI |
                      SQLITE_OPEN_SHAREDCACHE;

/** The instance structure for the client side PAM authenticator, created in pam_auth_init() */
struct PamInstance
{
    sqlite3 *m_dbhandle;         /**< SQLite3 database handle */
};

/** Used by the PAM conversation function */
struct ConversationData
{
    string password;
    int counter;
    DCB* client;
};

namespace
{
/**
 * @brief Add new PAM user entry to the internal user database
 *
 * @param handle Database handle
 * @param user   Username
 * @param host   Host
 * @param db     Database
 * @param anydb  Global access to databases
 * @param pam_service The PAM service used
 */
void add_pam_user(sqlite3 *handle, const char *user, const char *host,
                  const char *db, bool anydb, const char *pam_service)
{
    /**
     * The insert query template which adds users to the pam_users table.
     *
     * Note that 'db' and 'pam_service' are strings that can be NULL and thus they have
     * no quotes around them. The quotes for strings are added in this function.
     */
    const char insert_sql_pattern[] =
        "INSERT INTO " PAM_TABLE_NAME " VALUES ('%s', '%s', %s, '%s', %s)";

    /** Used for NULL value creation in the INSERT query */
    const char NULL_TOKEN[] = "NULL";

    size_t dblen = db ? strlen(db) + 2 : sizeof(NULL_TOKEN); /** +2 for single quotes */
    char dbstr[dblen + 1];

    if (db)
    {
        sprintf(dbstr, "'%s'", db);
    }
    else
    {
        strcpy(dbstr, NULL_TOKEN);
    }

    size_t servlen = (pam_service && *pam_service) ? strlen(pam_service) + 2 :
                     sizeof(NULL_TOKEN); /** +2 for single quotes */
    char service_string[servlen + 1];

    if (pam_service && *pam_service)
    {
        sprintf(service_string, "'%s'", pam_service);
    }
    else
    {
        strcpy(service_string, NULL_TOKEN);
    }

    size_t len = sizeof(insert_sql_pattern) + strlen(user) + strlen(host) + dblen + servlen + 1;

    char insert_sql[len + 1];
    sprintf(insert_sql, insert_sql_pattern, user, host, dbstr, anydb ? "1" : "0", service_string);

    char *err;
    if (sqlite3_exec(handle, insert_sql, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to insert user: %s", err);
        sqlite3_free(err);
    }

    MXS_INFO("Added user: %s", insert_sql);
}

/** Callback for print_db */
int print_DB_cb(void *data, int columns, char** column_vals, char** column_names)
{
    StringArray* results = static_cast<StringArray*>(data);
    std::stringstream message;
    message << "Row with " << columns << " items: ";

    for (int i = 0; i < columns; i++)
    {
        if (column_vals[i])
        {
            message << column_names[i] << " = " << column_vals[i];
        }
        else
        {
            message << column_names[i] << " = (null)";
        }
        message << " ,";
    }
    MXS_DEBUG("%s", message.str().c_str());

    if (column_vals[4])
    {
        results->push_back(column_vals[4]);
    }
    else
    {
        results->push_back("");
    }
    return 0;
}

int user_services_cb(void *data, int columns, char** column_vals, char** column_names)
{
    if (columns == 1)
    {
        StringArray* results = static_cast<StringArray*>(data);
        if (column_vals[0])
        {
            results->push_back(column_vals[0]);
        }
        else
        {
            // Empty is a valid value.
            results->push_back("");
        }
    }
    else
    {
        ss_dassert(!true);
    }
    return 0;
}

/**
 * @brief Create an AuthSwitchRequest packet
 *
 * This function also contains the first part of the PAM authentication. The server
 * (MaxScale) sends the plugin name "dialog" to the client with the first password
 * prompt. We want to avoid calling the PAM conversation function more than once
 * because it blocks, so we "emulate" its behaviour here. This obviously only works
 * with the basic password authentication scheme.
 *
 * @return Allocated packet
 * @see https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
 */
Buffer create_auth_change_packet(PamSession *pses)
{
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * byte        - Message type
     * string[EOF] - Message
     */
    size_t plen = 1 + sizeof(DIALOG) + 1 + sizeof(PASSWORD) - 1;
    size_t buflen = MYSQL_HEADER_LEN + plen;
    uint8_t data[buflen];
    uint8_t* pData = data;
    gw_mysql_set_byte3(pData, plen);
    pData += 3;
    *pData++ = ++pses->m_sequence; // Second packet
    *pData++ = 0xfe; // AuthSwitchRequest command
    memcpy(pData, DIALOG, sizeof(DIALOG)); // Plugin name
    pData += sizeof(DIALOG);
    *pData++ = DIALOG_ECHO_DISABLED;
    memcpy(pData, PASSWORD, sizeof(PASSWORD) - 1); // First message

    Buffer buffer(data, buflen);
    return buffer;
}

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

int conversation_func(int num_msg, const struct pam_message **msg,
                      struct pam_response **resp_out, void *appdata_ptr)
{
    MXS_DEBUG("Entering PAM conversation function.");
    ConversationData* data = static_cast<ConversationData*>(appdata_ptr);
    if (num_msg == 1)
    {
        pam_message first = *msg[0];
        MXS_DEBUG("Message type: '%d', contents:'%s'", first.msg_style, first.msg);
        pam_response* response = static_cast<pam_response*>(MXS_MALLOC(sizeof(pam_response)));
        if (response)
        {
            response[0].resp_retcode = 0;
            response[0].resp = MXS_STRDUP(data->password.c_str());
        }
        *resp_out = response;
    }
    else
    {
        MXS_ERROR("Conversation function received more than one message ('%d') from API.", num_msg);
    }
    data->counter++;
    return 0;
}

/**
 * @brief Check if the client token is valid
 *
 * @param token Client token
 * @param len Length of the token
 * @param output Pointer where the client principal name is stored
 * @return True if client token is valid
 */
bool validate_pam_password(string user, string password, string service, DCB* client)
{
    ConversationData appdata = {password, 0, client};
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
            // This shouldn't happen, normally at least
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

/**
 * @brief Verify the user has access to the database
 *
 * @param auth Authenticator session
 * @param dcb Client DCB
 * @param session MySQL session
 * @param princ Client principal name
 * @return True if the user has access to the database
 */
StringArray get_pam_user_services(PamSession *auth, DCB *dcb, MYSQL_session *session)
{
    string sql = string("SELECT authentication_string FROM " PAM_TABLE_NAME " WHERE user = '") +
                 session->user + "' AND '" + dcb->remote + "' LIKE host AND (anydb = '1' OR '" +
                 session->db + "' = '' OR '" + session->db + "' LIKE db) ORDER BY authentication_string";
    bool try_again = true;
    char *err;
    MXS_DEBUG("PAM services search sql: '%s'.", sql.c_str());
    StringArray service_names;
    /**
     * Try search twice: first time with the current users, second
     * time with fresh users.
     */
    for (int i = 0; i < 2 && try_again; i++)
    {
        if (sqlite3_exec(auth->m_dbhandle, sql.c_str(), user_services_cb,
                         &service_names, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to execute query: '%s'", err);
            sqlite3_free(err);
        }
        else if (service_names.size())
        {
            try_again = false;
            MXS_INFO("User '%s' matched %lu rows in " PAM_TABLE_NAME " db.",
                     session->user, service_names.size());
        }

        if (try_again && !i)
        {
            try_again = !service_refresh_users(dcb->service);
        }
    }
    return service_names;
}

/**
 * @brief Delete old users from the database
 * @param handle Database handle
 */
void delete_old_users(sqlite3 *handle)
{
    /** Delete query used to clean up the database before loading new users */
    const char DELETE_QUERY[] = "DELETE FROM " PAM_TABLE_NAME;
    char *err;
    if (sqlite3_exec(handle, DELETE_QUERY, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to delete old users: %s", err);
        sqlite3_free(err);
    }
}

/**
 * Prints auth database contents. Currently unused, may be useful for debugging.
 *
 * @param db Database handle
 */
void print_DB(sqlite3* db)
{
    MXS_NOTICE("PRINTING DB----------------");
    const char query[] = "SELECT * FROM " PAM_TABLE_NAME;
    char* err = NULL;
    StringArray services;
    if (sqlite3_exec(db, query, print_DB_cb, &services, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to execute auth query: %s", err);
        sqlite3_free(err);
    }
    MXS_NOTICE("DONE, got %lu users----------------", services.size());
}

}
/**
 * Initialize PAM authenticator
 *
 * @param options Listener options
 *
 * @return Authenticator instance
 */
void* pam_auth_init(char **options)
{
    /** CREATE TABLE statement for the in-memory table */
    const char CREATE_SQL[] = "CREATE TABLE IF NOT EXISTS " PAM_TABLE_NAME
                              " (user varchar(255), host varchar(255), db varchar(255), "
                              "anydb boolean, authentication_string text)";

    PamInstance *instance = new (std::nothrow) PamInstance();
    if (instance)
    {
        if (sqlite3_threadsafe() == 0)
        {
            MXS_ERROR("SQLite3 was compiled with thread safety off. May cause "
                      "corruption of in-memory database.");
        }
        /* This handle may be used from multiple threads, set full mutex. */
        if (sqlite3_open_v2(PAM_DATABASE_NAME, &instance->m_dbhandle,
                            db_flags | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK)
        {
            MXS_ERROR("Failed to open SQLite3 handle.");
            delete instance;
            return NULL;
        }

        char *err;
        if (sqlite3_exec(instance->m_dbhandle, CREATE_SQL, NULL, NULL, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to create database: '%s'", err);
            sqlite3_free(err);
            delete instance;
            return NULL;
        }
    }
    return instance;
}

/**
 * Allocate DCB-specific authenticator data
 *
 * @param instance Authenticator instance
 *
 * @return Authenticator session
 */
void* pam_auth_alloc(void *instance)
{
    PamSession* pses = new (std::nothrow) PamSession;
    if (pses)
    {
        // This handle is only used from one thread
        if (sqlite3_open_v2(PAM_DATABASE_NAME, &pses->m_dbhandle, db_flags, NULL) == SQLITE_OK)
        {
            sqlite3_busy_timeout(pses->m_dbhandle, MXS_SQLITE_BUSY_TIMEOUT);
        }
        else
        {
            MXS_ERROR("Failed to open SQLite3 handle.");
            delete pses;
            pses = NULL;
        }
    }
    return pses;
}

/**
 * Free authenticator session
 *
 * @param data PAM session
 */
void pam_auth_free(void *data)
{
    delete (PamSession*)data;
}

/**
 * @brief Extract data from client response
 *
 * @param dcb Client DCB
 * @param read_buffer Buffer containing the client's response
 * @return MXS_AUTH_SUCCEEDED if authentication can continue, MXS_AUTH_FAILED if
 * authentication failed
 */
static int pam_auth_extract(DCB *dcb, GWBUF *read_buffer)
{
    int rval = MXS_AUTH_FAILED;
    PamSession *pses = static_cast<PamSession*>(dcb->authenticator_data);
    gwbuf_copy_data(read_buffer, MYSQL_SEQ_OFFSET, 1, &pses->m_sequence);

    switch (pses->m_state)
    {
    case PAM_AUTH_INIT:
        rval = MXS_AUTH_SUCCEEDED;
        break;

    case PAM_AUTH_DATA_SENT:
        store_client_password(dcb, read_buffer);
        rval = MXS_AUTH_SUCCEEDED;
        break;

    default:
        MXS_ERROR("Unexpected authentication state: %d", pses->m_state);
        ss_dassert(!true);
        break;
    }
    return rval;
}

/**
 * @brief Is the client SSL capable
 *
 * @param dcb Client DCB
 * @return True if client supports SSL
 */
bool pam_auth_connectssl(DCB *dcb)
{
    MySQLProtocol *protocol = (MySQLProtocol*)dcb->protocol;
    return protocol->client_capabilities & GW_MYSQL_CAPABILITIES_SSL;
}

/**
 * @brief Authenticate the client
 *
 * @param dcb Client DCB
 * @return MXS_AUTH_INCOMPLETE if authentication is not yet complete, MXS_AUTH_SUCCEEDED
 * if authentication was successfully completed or MXS_AUTH_FAILED if authentication
 * has failed.
 */
int pam_auth_authenticate(DCB *dcb)
{
    int rval = MXS_AUTH_FAILED;
    PamSession *auth_ses = static_cast<PamSession*>(dcb->authenticator_data);

    if (auth_ses->m_state == PAM_AUTH_INIT)
    {
        /** We need to send the authentication switch packet to change the
         * authentication to something other than the 'mysql_native_password'
         * method */
        Buffer authbuf = create_auth_change_packet(auth_ses);
        if (authbuf.length() && dcb->func.write(dcb, authbuf.release()))
        {
            auth_ses->m_state = PAM_AUTH_DATA_SENT;
            rval = MXS_AUTH_INCOMPLETE;
        }
    }
    else if (auth_ses->m_state == PAM_AUTH_DATA_SENT)
    {
        /** We sent the authentication change packet + plugin name and the client
         * responded with the password. Try to continue authentication without more
         * messages to client. */
        MYSQL_session *ses = (MYSQL_session*)dcb->data;
        string password((char*)ses->auth_token, ses->auth_token_len);
        StringArray services = get_pam_user_services(auth_ses, dcb, ses);

        bool pam_passed = false;
        for (size_t i = 0; i < services.size() && !pam_passed; i++)
        {
            pam_passed = validate_pam_password(ses->user, password, services.at(i), dcb);
        }
        if (pam_passed)
        {
            rval = MXS_AUTH_SUCCEEDED;
        }
    }
    return rval;
}

/**
 * @brief Free authenticator data from a DCB
 *
 * @param dcb DCB to free
 */
void pam_auth_free_data(DCB *dcb)
{
    if (dcb->data)
    {
        MYSQL_session *ses = (MYSQL_session *)dcb->data;
        MXS_FREE(ses->auth_token);
        MXS_FREE(ses);
        dcb->data = NULL;
    }
}

/**
 * @brief Load database users that use PAM authentication
 *
 * Loading the list of database users that use the 'pam' plugin allows us to
 * give more precise error messages to the clients when authentication fails.
 *
 * @param listener Listener definition
 * @return MXS_AUTH_LOADUSERS_OK on success, MXS_AUTH_LOADUSERS_ERROR on error
 */
int pam_auth_load_users(SERV_LISTENER *listener)
{
    /** Query that gets all users that authenticate via the pam plugin */
    const char PAM_USERS_QUERY[] =
        "SELECT u.user, u.host, d.db, u.select_priv, u.authentication_string FROM "
        "mysql.user AS u LEFT JOIN mysql.db AS d "
        "ON (u.user = d.user AND u.host = d.host) WHERE u.plugin = 'pam' "
        "UNION "
        "SELECT u.user, u.host, t.db, u.select_priv, u.authentication_string FROM "
        "mysql.user AS u LEFT JOIN mysql.tables_priv AS t "
        "ON (u.user = t.user AND u.host = t.host) WHERE u.plugin = 'pam' "
        "ORDER BY user";

    char *user, *pw;
    int rval = MXS_AUTH_LOADUSERS_ERROR;
    PamInstance *inst = (PamInstance*)listener->auth_instance;

    if (serviceGetUser(listener->service, &user, &pw) && (pw = decrypt_password(pw)))
    {
        for (SERVER_REF *servers = listener->service->dbref; servers; servers = servers->next)
        {
            MYSQL *mysql = mysql_init(NULL);
            if (mxs_mysql_real_connect(mysql, servers->server, user, pw))
            {
                if (mysql_query(mysql, PAM_USERS_QUERY))
                {
                    MXS_ERROR("Failed to query server '%s' for PAM users: '%s'.",
                              servers->server->unique_name, mysql_error(mysql));
                }
                else
                {
                    MYSQL_RES *res = mysql_store_result(mysql);
                    delete_old_users(inst->m_dbhandle);

                    if (res)
                    {
                        ss_dassert(mysql_num_fields(res) == PAM_USERS_QUERY_NUM_FIELDS);
                        MXS_NOTICE("Read %llu rows when fetching users.", mysql_num_rows(res));
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(res)))
                        {
                            add_pam_user(inst->m_dbhandle, row[0], row[1], row[2],
                                         row[3] && strcasecmp(row[3], "Y") == 0,
                                         row[4]);
                        }
                        rval = MXS_AUTH_LOADUSERS_OK;
                        mysql_free_result(res);
                    }
                }

                mysql_close(mysql);

                if (rval == MXS_AUTH_LOADUSERS_OK)
                {
                    break;
                }
            }
        }
        MXS_FREE(pw);
    }

    return rval;
}

MXS_BEGIN_DECLS
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_AUTHENTICATOR MyObject =
    {
        pam_auth_init,                /* Initialize authenticator */
        pam_auth_alloc,               /* Allocate authenticator data */
        pam_auth_extract,             /* Extract data into structure   */
        pam_auth_connectssl,          /* Check if client supports SSL  */
        pam_auth_authenticate,        /* Authenticate user credentials */
        pam_auth_free_data,           /* Free the client data held in DCB */
        pam_auth_free,                /* Free authenticator data */
        pam_auth_load_users,          /* Load database users */
        users_default_diagnostic,        /* Default user diagnostic */
        users_default_diagnostic_json,   /* Default user diagnostic */
        NULL                             /* No user reauthentication */
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "PAM authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        { { MXS_END_MODULE_PARAMS} }
    };

    return &info;
}
MXS_END_DECLS
