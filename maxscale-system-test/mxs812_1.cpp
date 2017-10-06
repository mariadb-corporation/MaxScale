/**
 * @file mxs812_1.cpp - Checks "Current no. of conns" maxadmin output after long blob inserting
 * - set global max_allowed_packet=10000000
 * - pretare statement INSERT INTO long_blob_table(x, b) VALUES(1, ?)
 * - load chunks
 * - execute statement
 * - wait 5 seconds
 * - check "Current no. of conns" maxadmin output, expect 0
 * - repeat test 10 times
 */


#include "testconnections.h"

void run_test(TestConnections *Test, size_t size, int chunks)
{
    char *insert_stmt = (char *) "INSERT INTO long_blob_table(x, b) VALUES(1, ?)";
    MYSQL *conn = Test->maxscales->conn_rwsplit[0];
    MYSQL_STMT * stmt = mysql_stmt_init(conn);

    Test->tprintf("Preparing statement");
    Test->add_result(mysql_stmt_prepare(stmt, insert_stmt, strlen(insert_stmt)), "Error preparing stmt: %s\n",
                     mysql_stmt_error(stmt));

    MYSQL_BIND param[1];
    param[0].buffer_type = MYSQL_TYPE_STRING;
    param[0].is_null = 0;

    Test->add_result(mysql_stmt_bind_param(stmt, param), "Error parameter binding: %s\n", mysql_stmt_error(stmt));

    unsigned long *data = (unsigned long *) malloc(size * sizeof(long int));

    memset(data, '.', size * sizeof(long int));

    Test->tprintf("Sending %d x %d bytes of data", size, chunks);
    for (int i = 0; i < chunks; i++)
    {
        Test->set_timeout(600);
        Test->tprintf("Chunk #%d\n", i);
        if (mysql_stmt_send_long_data(stmt, 0, (char *) data, size * sizeof(long int)) != 0)
        {
            Test->add_result(1, "Error inserting data, iteration %d, error %s\n", i, mysql_stmt_error(stmt));
            return;
        }
    }

    Test->set_timeout(600);
    Test->tprintf("Executing statement");
    Test->add_result(mysql_stmt_execute(stmt), "INSERT Statement with BLOB failed, error is %s\n",
                     mysql_stmt_error(stmt));

    Test->stop_timeout();
    sleep(5);
    Test->check_current_operations(0, 0);
    Test->tprintf("Closing statement");
    Test->add_result(mysql_stmt_close(stmt), "Error closing stmt\n");
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(300);
    int iter = 10;

    Test->repl->execute_query_all_nodes( (char *) "set global max_allowed_packet=10000000");

    /** Create test table */
    Test->repl->connect();
    Test->try_query(Test->repl->nodes[0], (char*)"DROP TABLE IF EXISTS long_blob_table");
    Test->try_query(Test->repl->nodes[0], (char*)"CREATE TABLE long_blob_table(x INT, b BLOB)");

    Test->maxscales->connect_maxscale(0);
    Test->tprintf("Starting test");
    for (int i = 0; i < iter; i++)
    {
        run_test(Test, 500000, 10);
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
