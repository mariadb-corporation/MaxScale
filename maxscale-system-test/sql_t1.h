#ifndef SQL_T1_H
#define SQL_T1_H

#include "mariadb_func.h"
#include "testconnections.h"

/**
 * @brief execute_select_query_and_check Execute query and check that result contains expected number of rows
 * @param conn MYSQL handler
 * @param sql Query
 * @param rows Expected number of rows
 * @return 0 in case of success
 */
int execute_select_query_and_check(MYSQL* conn, const char* sql, unsigned long long int rows);

/**
 * @brief create_t1 Create t1 table, fileds: (x1 int, fl int)
 * @param conn MYSQL handler
 * @return 0 in case of success
 */
int create_t1(MYSQL* conn);


/**
 * @brief create_t1 Create t2 table, fileds: (x1 int, fl int)
 * @param conn MYSQL handler
 * @return 0 in case of success
 */
int create_t2(MYSQL* conn);

/**
 * @brief create_insert_string Create SQL query string to insert N rows into t1
 * fl is equal to given value and x1 is incrementing value (row index)
 * @param sql pointer to buffer to put result
 * @param N Number of rows to insert
 * @param fl value to fill 'fl' field
 * @return 0
 */
int create_insert_string(char* sql, int N, int fl);

/**
 * @brief create_insert_string Create SQL query string to insert N rows into t1
 * fl is equal to given value and x1 is incrementing value (row index)
 * (same as create_insert_string(), but allocates buffer for SQL string by itself)
 * @param sql pointer to buffer to put result
 * @param N Number of rows to insert
 * @param fl value to fill 'fl' field
 * @return pointer to insert SQL string
 */
char* allocate_insert_string(int fl, int N);

/**
 * @brief insert_into_t1 Insert N blocks of 16^i rows into t1
 * first block has fl=0, second - fl=1, ..., N-block fl=N-1
 * first block has 16 row, second - 256, ..., N-block 16^N rows
 * @param conn MYSQL handler
 * @param N Number of blocks to insert
 * @return 0 in case of success
 */
int insert_into_t1(MYSQL* conn, int N);

/**
 * @brief select_from_t1 Check that t1 contains data as inserted by insert_into_t1()
 * @param conn MYSQL handler
 * @param N Number of blocks to insert
 * @return 0 in case of success
 */
int select_from_t1(MYSQL* conn, int N);

/**
 * @brief check_if_t1_exists
 * @param conn MYSQL handler
 * @return 0 if content of t1 is ok
 */
int check_if_t1_exists(MYSQL* conn);

#endif      // SQL_T1_H
