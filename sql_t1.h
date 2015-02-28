#ifndef SQL_T1_H
#define SQL_T1_H

#include "mariadb_func.h"
#include "testconnections.h"

/**
Executes SQL query 'sql' using 'conn' connection and print results
*/
int execute_select_query_and_check(MYSQL *conn, char *sql, unsigned long long int rows);
int create_t1(MYSQL * conn);
int create_insert_string(char *sql, int N, int fl);
int insert_into_t1(MYSQL *conn, int N);
int select_from_t1(MYSQL *conn, int N);
int check_if_t1_exists(MYSQL *conn);

/**
 * @brief Creats t1 table, insert data into it and checks if data can be correctly read from all Maxscale services
 * @param Test Pointer to TestConnections object that contains references to test setup
 * @param N number of INSERTs; every next INSERT is longer 16 times in compare with previous one: for N=4 last INSERT is about 700kb long
 * @return 0 in case of no error and all checks are ok
 */
int insert_select(TestConnections* Test, int N);

/**
 * @brief Executes USE command for all Maxscale service and all Master/Slave backend nodes
 * @param Test Pointer to TestConnections object that contains references to test setup
 * @param db Name of DB in 'USE' command
 * @return 0 in case of success
 */
int use_db(TestConnections* Test, char * db);

/**
 * @brief Checks if table t1 exists in DB
 * @param Test Pointer to TestConnections object that contains references to test setup
 * @param presence expected result
 * @param db DB name
 * @return 0 if (t1 table exists AND presence=TRUE) OR (t1 table does not exist AND presence=FALSE)
 */

int check_t1_table(TestConnections* Test, bool presence, char * db);




#endif // SQL_T1_H
