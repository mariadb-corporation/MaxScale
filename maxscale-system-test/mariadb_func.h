#ifndef MARIADB_FUNC_H
#define MARIADB_FUNC_H


/**
 * @file mariadb_func.h - basic DB interaction routines
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 17/11/14 Timofey Turenko Initial implementation
 *
 * @endverbatim
 */

#include <mariadb/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <string>

/**
 * Opens connection to DB: wropper over mysql_real_connect
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param db    name of DB to connect
 * @param User  User name
 * @param Password  Password
 * @param flag  Connections flags
 * @param ssl   true if ssl should be used
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn_db_flags(int port, const char* ip, const char* db, const char* User, const char* Password,
                           unsigned long flag, bool ssl);


/**
 * Opens connection to DB: wropper over mysql_real_connect
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param db    name of DB to connect
 * @param User  User name
 * @param Password  Password
 * @param timeout  timeout on seconds
 * @param ssl   true if ssl should be used
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn_db_timeout(int port, const char* ip, const char* db, const char* User, const char* Password,
                             unsigned long timeout, bool ssl);

MYSQL* open_conn_db_timeout(int port, const std::string& ip, const std::string& db,
                            const std::string& user, const std::string& password,
                            unsigned long timeout, bool ssl);

/**
 * Opens connection to DB with default flags
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param db    name of DB to connect
 * @param User  User name
 * @param Password  Password
 * @param ssl   true if ssl should be used
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn_db(int port, const char* ip, const char* db, const char* User, const char* Password,
                     bool ssl);


/**
 * Opens connection to 'test' with default flags
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param User  User name
 * @param Password  Password
 * @param ssl   true if ssl should be used
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn(int port, const char* ip, const char* User, const char* Password, bool ssl);

/**
 * Opens connection to with default flags without defning DB name (just conecto server)
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param User  User name
 * @param Password  Password
 * @param ssl   true if ssl should be used
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn_no_db(int port, const char* ip, const char* User, const char* Password, bool ssl);

/**
 * @brief set_ssl Configure SSL for given connection
 * Function assumes that certificates are in test_dir/ssl-cert/ directory
 * @param conn MYSQL handler
 * @return return of mysql_ssl_set() (always 0, see mysql_ssl_set() documentation)
 */
int set_ssl(MYSQL * conn);

/**
 * @brief Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clean up returns
 * @param conn      MYSQL connection
 * @param format    SQL string with printf style formatting
 * @param ...       Parameters for @c format
 * @return 0 in case of success
 */
int execute_query(MYSQL *conn, const char *format, ...);

/**
 * @brief execute_query_from_file Read a line from a file, trim leading and trailing whitespace and execute it.
 * @param conn MYSQL handler
 * @param file file handler
 * @return 0 in case of success
 */
int execute_query_from_file(MYSQL *conn, FILE *file);

/**
 * @brief Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clean up returns
 * @param conn MYSQL connection struct
 * @param sql   SQL string
 * @return 0 in case of success
 */
int execute_query_silent(MYSQL *conn, const char *sql);

/**
 * @brief Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clean up returns
 * This function do not support 'printf' format for sql (in compare with execute_query()
 * @param conn MYSQL    connection struct
 * @param sql   SQL string
 * @param silent if true function do not produce any printing
 * @return 0 in case of success
 */
int execute_query1(MYSQL *conn, const char *sql, bool silent);

/**
 * @brief Executes SQL query and store 'affected rows' number in affectet_rows parameter
 * @param conn MYSQL    connection struct
 * @param sql   SQL string
 * @param affected_rows pointer to variabe to store number of affected rows
 * @return 0 in case of success
 */
int execute_query_affected_rows(MYSQL *conn, const char *sql, my_ulonglong * affected_rows);

/**
* @brief A more convenient form of execute_query_affected_rows()
*
* @param conn Connection to use for the query
* @param sql  The SQL statement to execute
* @return Number of rows or -1 on error
*/
int execute_query_count_rows(MYSQL *conn, const char *sql);

/**
 * @brief Executes SQL query and get number of rows in the result
 * This function does not check boudaries of 'num_of_rows' array. This
 * array have to be big enough to store all results
 * @param conn MYSQL    connection struct
 * @param sql   SQL string
 * @param num_of_rows pointer to array to store number of result rows
 * @param i pointer to variable to store number of result sets
 * @return 0 in case of success
 */
int execute_query_num_of_rows(MYSQL *conn, const char *sql, my_ulonglong num_of_rows[],
                              unsigned long long *i);

/**
 * @brief Executes perared statement and get number of rows in the result
 * This function does not check boudaries of 'num_of_rows' array. This
 * array have to be big enough to store all results
 * @param stmt MYSQL_STMT statetement struct (from mysql_stmt_init())
 * @param num_of_rows pointer to array to store number of result rows
 * @param i pointer to variable to store number of result sets
 * @return 0 in case of success
 */
int execute_stmt_num_of_rows(MYSQL_STMT *stmt, my_ulonglong num_of_rows[], unsigned long long * i);

/**
 * @brief execute_query_check_one Executes query and check if first field of first row is equal to 'expected'
 * @param conn MYSQL handler
 * @param sql query SQL query to execute
 * @param expected Expected result
 * @return 0 in case of success
 */
int execute_query_check_one(MYSQL *conn, const char *sql, const char *expected);

/**
 * @brief Executes 'show processlist' and calculates number of connections from defined host to defined DB
 * @param conn MYSQL    connection struct
 * @param ip    connections from this IP address are counted
 * @param db    name of DB to which connections are counted
 * @return number of connections
 */
int get_conn_num(MYSQL *conn, const char* ip, const char* hostname, const char* db);

/**
 * @brief Find given filed in the SQL query reply
 * Function checks only firs row from the table
 * @param conn MYSQL    connection struct
 * @param sql   SQL query to execute
 * @param filed_name    name of field to find
 * @param value pointer to variable to store value of found field
 * @return 0 in case of success
 */
int find_field(MYSQL *conn, const char * sql, const char * field_name, char * value);

/**
 * @brief Return the value of SECONDS_BEHIND_MASTER
 * @param conn MYSQL    connection struct
 * @return value of SECONDS_BEHIND_MASTER
 */
unsigned int get_seconds_behind_master(MYSQL *conn);


/**
 * @brief Read MaxScale log file
 * @param name  Name of log file (full path)
 * @param err_log_content   pointer to the buffer to store log file content
 * @return 0 in case of success, 1 in case of error
 */
int read_log(const char* name, char **err_log_content_p);

int get_int_version(const std::string& version);
int get_int_version(const char* version);

#endif // MARIADB_FUNC_H
