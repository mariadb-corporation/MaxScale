#include "big_transaction.h"

int big_transaction(MYSQL * conn, int N)
{
    int local_result = 0;
    char sql[1000000];
    local_result += create_t1(conn);
    local_result += execute_query(conn, (char *) "START TRANSACTION");
    local_result += execute_query(conn, (char *) "SET autocommit = 0");

    for (int i = 0; i < N; i++)
    {
        create_insert_string(sql, 10000, i);
        local_result += execute_query(conn, sql);
        local_result += execute_query(conn, "CREATE TABLE t2(id int);");
        local_result += execute_query(conn, sql);
        local_result += execute_query(conn, "DROP TABLE t2;");
        local_result += execute_query(conn, sql);
    }

    local_result += execute_query(conn, (char *) "COMMIT");
    return local_result;
}
