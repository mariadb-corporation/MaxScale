/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "pam_auth.hh"

#include <string>
#include <maxscale/authenticator.hh>
#include <maxscale/users.h>
#include <maxbase/format.hh>

#include "pam_instance.hh"
#include "pam_client_session.hh"
#include "../pam_auth_common.hh"

using std::string;
using SSQLite = SQLite::SSQLite;

/** Table and column names. The names mostly match the ones in the server. */
const string TABLE_USER = "user";
const string TABLE_DB = "db";
const string TABLE_ROLES_MAPPING = "roles_mapping";

const string FIELD_USER = "user";
const string FIELD_HOST = "host";
const string FIELD_AUTHSTR = "authentication_string";
const string FIELD_DEF_ROLE = "default_role";
const string FIELD_ANYDB = "anydb";
const string FIELD_IS_ROLE = "is_role";
const string FIELD_HAS_PROXY = "proxy_grant";

const string FIELD_DB = "db";
const string FIELD_ROLE = "role";

const char* SQLITE_OPEN_FAIL = "Failed to open SQLite3 handle for file '%s': '%s'";
const char* SQLITE_OPEN_OOM = "Failed to allocate memory for SQLite3 handle for file '%s'.";

using SSQLite = SQLite::SSQLite;

SSQLite SQLite::create(const string& filename, int flags, string* error_out)
{
    SSQLite rval;
    sqlite3* dbhandle = nullptr;
    const char* zFilename = filename.c_str();
    int ret = sqlite3_open_v2(zFilename, &dbhandle, flags, NULL);
    string error_msg;
    if (ret == SQLITE_OK)
    {
        rval.reset(new SQLite(dbhandle));
    }
    // Even if the open failed, the handle may exist and an error message can be read.
    else if (dbhandle)
    {
        error_msg = mxb::string_printf(SQLITE_OPEN_FAIL, zFilename, sqlite3_errmsg(dbhandle));
        sqlite3_close_v2(dbhandle);
    }
    else
    {
        error_msg = mxb::string_printf(SQLITE_OPEN_OOM, zFilename);
    }

    if (!error_msg.empty() && error_out)
    {
        *error_out = error_msg;
    }
    return rval;
}

SQLite::SQLite(sqlite3* handle)
    : m_dbhandle(handle)
{
    mxb_assert(handle);
}

SQLite::~SQLite()
{
    sqlite3_close_v2(m_dbhandle);
}

bool SQLite::exec(const std::string& sql)
{
    return exec_impl(sql, nullptr, nullptr);
}

bool SQLite::exec_impl(const std::string& sql, CallbackVoid cb, void* cb_data)
{
    char* err = nullptr;
    bool success = (sqlite3_exec(m_dbhandle, sql.c_str(), cb, cb_data, &err) == SQLITE_OK);
    if (success)
    {
        m_errormsg.clear();
    }
    else
    {
        m_errormsg = err;
        sqlite3_free(err);
    }
    return success;
}

void SQLite::set_timeout(int ms)
{
    sqlite3_busy_timeout(m_dbhandle, ms);
}

const char* SQLite::error() const
{
    return m_errormsg.c_str();
}

/**
 * Initialize PAM authenticator
 *
 * @param options Listener options
 *
 * @return Authenticator instance, or NULL on error
 */
static void* pam_auth_init(char** options)
{
    return PamInstance::create(options);
}

/**
 * Allocate DCB-specific authenticator data (session)
 *
 * @param instance Authenticator instance the session should be connected to
 *
 * @return Authenticator session
 */
static void* pam_auth_alloc(void* instance)
{
    PamInstance* inst = static_cast<PamInstance*>(instance);
    return PamClientSession::create(*inst);
}

/**
 * Free authenticator session
 *
 * @param data PAM session
 */
static void pam_auth_free(void* data)
{
    delete static_cast<PamClientSession*>(data);
}

/**
 * @brief Extract data from client response
 *
 * @param dcb Client DCB
 * @param read_buffer Buffer containing the client's response
 *
 * @return True if authentication can continue, false if
 * authentication failed
 */
static bool pam_auth_extract(DCB* dcb, GWBUF* read_buffer)
{
    PamClientSession* pses = static_cast<PamClientSession*>(dcb->authenticator_data);
    return pses->extract(dcb, read_buffer);
}

/**
 * @brief Is the client SSL capable
 *
 * @param dcb Client DCB
 *
 * @return True if client supports SSL
 */
static bool pam_auth_connectssl(DCB* dcb)
{
    MySQLProtocol* protocol = (MySQLProtocol*)dcb->protocol;
    return protocol->client_capabilities & GW_MYSQL_CAPABILITIES_SSL;
}

/**
 * @brief Authenticate the client. Should be called after pam_auth_extract().
 *
 * @param dcb Client DCB
 *
 * @return MXS_AUTH_INCOMPLETE if authentication is not yet complete. MXS_AUTH_SUCCEEDED
 * if authentication was successfully completed. MXS_AUTH_FAILED if authentication
 * has failed.
 */
static int pam_auth_authenticate(DCB* dcb)
{
    PamClientSession* pses = static_cast<PamClientSession*>(dcb->authenticator_data);
    return pses->authenticate(dcb);
}

/**
 * Free general authenticator data from a DCB. This is data that is not specific
 * to the client authenticator session and may be used by the backend authenticator
 * session to log onto backends.
 *
 * @param dcb DCB to free data from
 */
static void pam_auth_free_data(DCB* dcb)
{
    if (dcb->data)
    {
        MYSQL_session* ses = (MYSQL_session*)dcb->data;
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
 *
 * @return MXS_AUTH_LOADUSERS_OK on success, MXS_AUTH_LOADUSERS_ERROR on error
 */
static int pam_auth_load_users(Listener* listener)
{
    PamInstance* inst = static_cast<PamInstance*>(listener->auth_instance());
    return inst->load_users(listener->service());
}

static void pam_auth_diagnostic(DCB* dcb, Listener* listener)
{
    PamInstance* inst = static_cast<PamInstance*>(listener->auth_instance());
    inst->diagnostic(dcb);
}

static json_t* pam_auth_diagnostic_json(const Listener* listener)
{
    PamInstance* inst = static_cast<PamInstance*>(listener->auth_instance());
    return inst->diagnostic_json();
}

extern "C"
{
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_AUTHENTICATOR MyObject =
    {
        pam_auth_init,                  /* Initialize authenticator */
        pam_auth_alloc,                 /* Allocate authenticator data */
        pam_auth_extract,               /* Extract data into structure   */
        pam_auth_connectssl,            /* Check if client supports SSL  */
        pam_auth_authenticate,          /* Authenticate user credentials */
        pam_auth_free_data,             /* Free the client data held in DCB */
        pam_auth_free,                  /* Free authenticator data */
        pam_auth_load_users,            /* Load database users */
        pam_auth_diagnostic,            /* Default user diagnostic */
        pam_auth_diagnostic_json,       /* Default user diagnostic */
        NULL                            /* No user reauthentication */
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
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}
