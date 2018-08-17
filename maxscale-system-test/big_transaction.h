#ifndef BIG_TRANSACTION_H
#define BIG_TRANSACTION_H

#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include "sql_t1.h"

/**
 * @brief big_transaction Executes big transaction (includes N INSERTs of 10000 rows)
 * @param conn MYSQL connection handler
 * @param N Number of INSERTs
 * @return 0 if success
 */
int big_transaction(MYSQL * conn, int N);

#endif // BIG_TRANSACTION_H
