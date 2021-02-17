/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Loading MySQL users from a MySQL backend server
 */

#include "mysql_auth.hh"
#include <stdio.h>
#include <maxbase/alloc.h>


/** The table name where we store the users */
#define MYSQLAUTH_USERS_TABLE_NAME "mysqlauth_users"

/** Query that checks if there's a grant for the user being authenticated */
static const char mysqlauth_validate_user_query[] =
    "SELECT password FROM " MYSQLAUTH_USERS_TABLE_NAME
    " WHERE user = '%s' AND ( '%s' = host OR '%s' LIKE host)"
    " AND (anydb = '1' OR '%s' IN ('', 'information_schema') OR '%s' LIKE db)"
    " LIMIT 1";

/** Query that checks if there's a grant for the user being authenticated */
static const char mysqlauth_validate_user_query_lower[] =
    "SELECT password FROM " MYSQLAUTH_USERS_TABLE_NAME
    " WHERE user = '%s' AND ( '%s' = host OR '%s' LIKE host)"
    " AND (anydb = '1' OR LOWER('%s') IN ('', 'information_schema') OR LOWER('%s') LIKE LOWER(db))"
    " LIMIT 1";

/** Query that only checks if there's a matching user */
static const char mysqlauth_skip_auth_query[] =
    "SELECT password FROM " MYSQLAUTH_USERS_TABLE_NAME
    " WHERE user = '%s' AND (anydb = '1' OR '%s' IN ('', 'information_schema') OR '%s' LIKE db)"
    " LIMIT 1";

const char* xpand_users_query_format =
    "SELECT u.username AS user, u.host, a.dbname AS db, "
    "       IF(a.privileges & 1048576, 'Y', 'N') AS select_priv, u.password "
    "FROM system.users AS u LEFT JOIN system.user_acl AS a ON (u.user = a.role) "
    "WHERE u.plugin IN ('', 'mysql_native_password') %s";

static char* get_xpand_users_query(bool include_root)
{
    const char* with_root;

    if (include_root)
    {
        with_root =
            "UNION ALL "
            "SELECT 'root' AS user, '127.0.0.1', '*' AS db, 'Y' AS select_priv, '' AS password";
    }
    else
    {
        with_root = "AND u.username <> 'root'";
    }

    size_t n_bytes = snprintf(NULL, 0, xpand_users_query_format, with_root);
    char* rval = static_cast<char*>(MXS_MALLOC(n_bytes + 1));
    MXS_ABORT_IF_NULL(rval);
    snprintf(rval, n_bytes + 1, xpand_users_query_format, with_root);

    return rval;
}
