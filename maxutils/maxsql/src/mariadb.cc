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
#include <maxsql/mariadb.hh>
#include <time.h>
#include <errmsg.h>
#include <maxbase/assert.h>


namespace
{
struct THIS_UNIT
{
    bool log_statements;    // Should all statements sent to server be logged?
};

static THIS_UNIT this_unit =
{
    false
};
}

namespace maxsql
{

int mysql_query_ex(MYSQL* conn, const std::string& query, int query_retries, time_t query_retry_timeout)
{
    const char* query_cstr = query.c_str();
    time_t start = time(NULL);
    int rc = mysql_query(conn, query_cstr);

    for (int n = 0; rc != 0 && n < query_retries && mysql_is_net_error(mysql_errno(conn))
         && time(NULL) - start < query_retry_timeout; n++)
    {
        rc = mysql_query(conn, query_cstr);
    }

    if (this_unit.log_statements)
    {
        const char* host = "0.0.0.0";
        unsigned int port = 0;
        MXB_AT_DEBUG(int rc1 = ) mariadb_get_info(conn, MARIADB_CONNECTION_HOST, &host);
        MXB_AT_DEBUG(int rc2 = ) mariadb_get_info(conn, MARIADB_CONNECTION_PORT, &port);
        mxb_assert(!rc1 && !rc2);
        MXB_NOTICE("SQL([%s]:%u): %d, \"%s\"", host, port, rc, query_cstr);
    }

    return rc;
}

bool mysql_is_net_error(unsigned int errcode)
{
    switch (errcode)
    {
    case CR_SOCKET_CREATE_ERROR:
    case CR_CONNECTION_ERROR:
    case CR_CONN_HOST_ERROR:
    case CR_IPSOCK_ERROR:
    case CR_SERVER_GONE_ERROR:
    case CR_TCP_CONNECTION:
    case CR_SERVER_LOST:
        return true;

    default:
        return false;
    }
}

void mysql_set_log_statements(bool enable)
{
    this_unit.log_statements = enable;
}

bool mysql_get_log_statements()
{
    return this_unit.log_statements;
}
}
