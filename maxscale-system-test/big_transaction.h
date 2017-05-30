#ifndef BIG_TRANSACTION_H
#define BIG_TRANSACTION_H

#include <my_config.h>
#include <my_global.h>
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include "sql_t1.h"

int big_transaction(MYSQL * conn, int N);

#endif // BIG_TRANSACTION_H
