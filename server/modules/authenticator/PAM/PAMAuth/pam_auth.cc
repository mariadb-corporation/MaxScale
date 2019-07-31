/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
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

const int NUM_FIELDS = 6;

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

extern "C"
{
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "PAM authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApi<PamInstance>::s_api,
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
