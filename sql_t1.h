#ifndef SQL_T1_H
#define SQL_T1_H

#include "mariadb_func.h"

/**
Executes SQL query 'sql' using 'conn' connection and print results
*/
int execute_select_query_and_check(MYSQL *conn, char *sql, unsigned long long int rows);
int create_t1(MYSQL * conn);
int create_insert_string(char *sql, int N, int fl);
int insert_into_t1(MYSQL *conn, int N);
int select_from_t1(MYSQL *conn, int N);
int check_if_t1_exists(MYSQL *conn);

#endif // SQL_T1_H
