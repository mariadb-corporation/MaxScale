/**
 * @file longblob.cpp - trying to use LONGBLOB
 */

#include <my_config.h>
#include "testconnections.h"

int test_longblob(TestConnections* Test, MYSQL * conn)
{
    unsigned long size = 1000000;
    unsigned long * data;
    unsigned long i;
    MYSQL_BIND param[1];

    char *insert_stmt = (char *) "INSERT INTO long_blob_table(x, b) VALUES(1, ?)";



    Test->tprintf("Creating table with LONGBLOB\n");
    Test->try_query(conn, (char *) "DROP TABLE IF EXISTS long_blob_table");
    Test->try_query(conn, (char *) "CREATE TABLE long_blob_table(x INT, b LONGBLOB)");
    Test->tprintf("set global max_allowed_packet=10000000\n");
    Test->try_query(conn, (char *) "set global max_allowed_packet=10000000");

    Test->tprintf("Preparintg INSERT stmt\n");
    MYSQL_STMT * stmt = mysql_stmt_init(conn);

    Test->add_result(mysql_stmt_prepare(stmt, insert_stmt, strlen(insert_stmt)), "Error preparing stmt: %s\n", mysql_stmt_error(stmt));

    param[0].buffer_type = MYSQL_TYPE_STRING;
    param[0].is_null = 0;

    Test->tprintf("Binding parameter\n");
    Test->add_result(mysql_stmt_bind_param(stmt, param), "Error parameter binding: %s\n", mysql_stmt_error(stmt));

    Test->tprintf("Filling buffer\n");
    data = (unsigned long *) malloc(size * sizeof(long int));

    if (data == NULL)
    {
        Test->add_result(1, "Memory allocation error\n");
    }

    for (i = 0; i < size; i++)
    {
        data[i] = i;
    }

    Test->tprintf("Sending data in %d bytes chunks\n", size * sizeof(long int));
    for (i = 0; i < 100; i++) {
        Test->add_result(mysql_stmt_send_long_data(stmt, 0, (char *) data, size * sizeof(long int)), "Error inserting data, iteration %d, error %s\n", i, mysql_stmt_error(stmt));
    }

    Test->add_result(mysql_stmt_execute(stmt), mysql_stmt_error(stmt));
    Test->add_result(mysql_stmt_close(stmt), "Error closing stmt\n");

    return 0;
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(60);
    Test->connect_maxscale();

    test_longblob(Test, Test->conn_rwsplit);


    Test->copy_all_logs(); return(Test->global_result);
}
