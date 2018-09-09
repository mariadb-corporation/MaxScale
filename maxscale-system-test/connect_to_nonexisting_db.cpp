/**
 * @file connect_to_nonexisting_db.cpp Tries to connect to non existing DB, expects no crash
 */

#include "testconnections.h"
#include "sql_t1.h"

bool try_connect(TestConnections& test)
{
    const char* ip = test.maxscales->IP[0];
    const char* user = test.maxscales->user_name;
    const char* pw = test.maxscales->password;
    const char* db = "test_db";

    MYSQL* rwsplit = open_conn_db(test.maxscales->rwsplit_port[0], ip, db, user, pw, false);
    MYSQL* master = open_conn_db(test.maxscales->readconn_master_port[0], ip, db, user, pw, false);
    MYSQL* slave = open_conn_db(test.maxscales->readconn_slave_port[0], ip, db, user, pw, false);
    bool rval = false;

    if (rwsplit && master && slave
        && execute_query(rwsplit, "SELECT 1") == 0
        && execute_query(master, "SELECT 1") == 0
        && execute_query(slave, "SELECT 1") == 0)

    {
        rval = true;
    }

    mysql_close(rwsplit);
    mysql_close(master);
    mysql_close(slave);

    return rval;
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Connecting to RWSplit");

    test.set_timeout(30);
    MYSQL* conn = open_conn_no_db(test.maxscales->rwsplit_port[0],
                                  test.maxscales->IP[0],
                                  test.maxscales->user_name,
                                  test.maxscales->password,
                                  test.ssl);
    test.add_result(conn == NULL, "Error connecting to MaxScale");

    test.tprintf("Removing 'test_db' DB");
    execute_query(conn, "DROP DATABASE IF EXISTS test_db");
    test.tprintf("Closing connections and waiting 5 seconds");
    mysql_close(conn);
    test.stop_timeout();
    sleep(5);

    test.set_timeout(30);
    test.tprintf("Connection to non-existing DB (all maxscales->routers[0])");
    test.add_result(try_connect(test), "Connection with dropped database should fail");

    test.tprintf("Connecting to RWSplit again to recreate 'test_db' db");
    conn = open_conn_no_db(test.maxscales->rwsplit_port[0],
                           test.maxscales->IP[0],
                           test.maxscales->user_name,
                           test.maxscales->password,
                           test.ssl);
    test.add_result(conn == NULL, "Error connecting to MaxScale");

    test.tprintf("Creating and selecting 'test_db' DB");
    test.try_query(conn, "CREATE DATABASE test_db");
    test.try_query(conn, "USE test_db");
    test.tprintf("Creating 't1' table");
    test.add_result(create_t1(conn), "Error creation 't1'");
    mysql_close(conn);

    test.tprintf("Reconnectiong");
    test.add_result(!try_connect(test), "Error connecting to Maxscale");


    test.tprintf("Trying simple operations with t1 ");
    conn = open_conn_no_db(test.maxscales->rwsplit_port[0],
                           test.maxscales->IP[0],
                           test.maxscales->user_name,
                           test.maxscales->password,
                           test.ssl);
    test.try_query(conn, "USE test_db");
    test.try_query(conn, "INSERT INTO t1 (x1, fl) VALUES(0, 1)");
    test.set_timeout(60);
    test.add_result(execute_select_query_and_check(conn, "SELECT * FROM t1", 1),
                    "Error execution SELECT * FROM t1;");
    mysql_close(conn);

    return test.global_result;
}
