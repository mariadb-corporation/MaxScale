/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxsql/ccdefs.hh>
#include <string>
#include <mysql.h>

namespace maxsql
{
/**
 * Execute a query, manually defining retry limits.
 *
 * @param conn MySQL connection
 * @param query Query to execute
 * @param query_retries Maximum number of retries
 * @param query_retry_timeout Maximum time to spend retrying, in seconds
 * @return return value of mysql_query
 */
int mysql_query_ex(MYSQL* conn, const std::string& query, int query_retries, time_t query_retry_timeout);

/**
 * Check if the MYSQL error number is a connection error.
 *
 * @param Error code
 * @return True if the MYSQL error number is a connection error
 */
bool mysql_is_net_error(unsigned int errcode);

/**
 * Enable/disable the logging of all SQL statements MaxScale sends to
 * the servers.
 *
 * @param enable If true, enable, if false, disable.
 */
void mysql_set_log_statements(bool enable);

/**
 * Returns whether SQL statements sent to the servers are logged or not.
 *
 * @return True, if statements are logged, false otherwise.
 */
bool mysql_get_log_statements();
}
