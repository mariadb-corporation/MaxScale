#pragma once
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

#include <maxscale/cdefs.h>
#include <stdlib.h>
#include <stdint.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/server.h>

MXS_BEGIN_DECLS

/** Length-encoded integers */
size_t mxs_leint_bytes(const uint8_t* ptr);
uint64_t mxs_leint_value(const uint8_t* c);
uint64_t mxs_leint_consume(uint8_t ** c);

/** Length-encoded strings */
char* mxs_lestr_consume_dup(uint8_t** c);
char* mxs_lestr_consume(uint8_t** c, size_t *size);

/**
 * Creates a connection to a MySQL database engine. If necessary, initializes SSL.
 *
 * @param con    A valid MYSQL structure.
 * @param server The server on which the MySQL engine is running.
 * @param user   The MySQL login ID.
 * @param passwd The password for the user.
 *
 * @return New connection or NULL on error
 */
MYSQL* mxs_mysql_real_connect(MYSQL *mysql, SERVER *server, const char *user, const char *passwd);

/**
 * Check if the MYSQL error number is a connection error
 *
 * @return True if the MYSQL error number is a connection error
 */
bool mxs_mysql_is_net_error(int errcode);

/**
 * Execute a query
 *
 * This function wraps mysql_query in a way that automatic query retry is possible.
 *
 * @param conn  MySQL connection
 * @param query Query to execute
 *
 * @return return value of mysql_query
 */
int mxs_mysql_query(MYSQL* conn, const char* query);

/**
 * Trim MySQL quote characters surrounding a string.
 *
 *   'abcd' => abcd
 *   "abcd" => abcd
 *   `abcd` => abcd
 *
 * @param s  The string to be trimmed.
 *
 * @note The string is modified in place.
 */
bool mxs_mysql_trim_quotes(char *s);

/**
 * Helper function for getting values by field name
 *
 * @param result Resultset
 * @param row    Row where the value is read
 * @param key    Name of the field
 *
 * @return The value of the field or NULL if value is not found. NULL values
 *         are also returned as NULL pointers.
 */
const char* mxs_mysql_get_value(MYSQL_RES* result, MYSQL_ROW row, const char* key);

typedef enum mxs_pcre_quote_approach
{
    MXS_PCRE_QUOTE_VERBATIM, /*<! Quote all PCRE characters. */
    MXS_PCRE_QUOTE_WILDCARD  /*<! Quote all PCRE characters, except % that is converted into .*. */
} mxs_pcre_quote_approach_t;

typedef enum mxs_mysql_name_kind
{
    MXS_MYSQL_NAME_WITH_WILDCARD,   /*<! The input string contains a %. */
    MXS_MYSQL_NAME_WITHOUT_WILDCARD /*<! The input string does not contain a %. */
} mxs_mysql_name_kind_t;

/**
 * Convert a MySQL/MariaDB name string to a pcre compatible one.
 *
 * Note that the string is expected to be a user name or a host name,
 * but not a full account name. Further, if converting a user name,
 * then the approach should be @c MXS_PCRE_QUOTE_VERBATIM and if converting
 * a host name, the approach should be @c MXS_PCRE_QUOTE_WILDCARD.
 *
 * Note also that this function will not trim surrounding quotes.
 *
 * In principle:
 *   - Quote all characters that have a special meaning in a PCRE context.
 *   - Optionally convert "%" into ".*".
 *
 * @param pcre     The string to which the conversion should be copied.
 *                 To be on the safe size, the buffer should be twice the
 *                 size of 'mysql'.
 * @param mysql    The mysql user or host string.
 * @param approach Whether % should be converted or not.
 *
 * @return Whether or not the name contains a wildcard.
 */
mxs_mysql_name_kind_t mxs_mysql_name_to_pcre(char *pcre,
                                             const char *mysql,
                                             mxs_pcre_quote_approach_t approach);

/**
 * Set the server information
 *
 * @param mysql   A MySQL handle to the server.
 * @param server  The server whose version information should be updated.
 */
void mxs_mysql_set_server_version(MYSQL* mysql, SERVER* server);

/**
 * Enable/disable the logging of all SQL statements MaxScale sends to
 * the servers.
 *
 * @param enable If true, enable, if false, disable.
 */
void mxs_mysql_set_log_statements(bool enable);

/**
 * Returns whether SQL statements sent to the servers are logged or not.
 *
 * @return True, if statements are logged, false otherwise.
 */
bool mxs_mysql_get_log_statements();

MXS_END_DECLS
