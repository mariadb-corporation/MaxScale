/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxsql/ccdefs.hh>
#include <time.h>
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

/** Length-encoded integers */
size_t   leint_bytes(const uint8_t* ptr);
uint64_t leint_value(const uint8_t* c);
uint64_t leint_consume(uint8_t** c);

/** Length-encoded strings */
char*       lestr_consume_dup(uint8_t** c);
char*       lestr_consume(uint8_t** c, size_t* size);
const char* lestr_consume_safe(const uint8_t** c, const uint8_t* end, size_t* size);

// Logs the statement if statement logging is enabled
void log_statement(int rc, MYSQL* conn, const std::string& query);

/**
 * Get server capabilities
 *
 * @param conn Connection to use
 *
 * @return The 64 bits of capabilities. The lower 32 bits are the basic capabilities and the upper 32 bits are
 * the MariaDB extended ones.
 */
uint64_t mysql_get_server_capabilities(MYSQL* conn);

/**
 * Causes a PROXY UNKNOWN header to be sent when the connection is created
 *
 * If a server has proxy protocol enabled, internal connections to it should construct a valid proxy protocol
 * header. A valid header cannot be created with connector-c as the source address and port are unknown at the
 * time of creation. To still comply with the specification, a PROXY UNKNOWN header can be used.
 *
 * @param conn The connection to use
 */
void set_proxy_header(MYSQL* conn);
}
