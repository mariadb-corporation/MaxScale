/**
 * @file mxs716.cpp Test for MXS-716 ("Access Denied: User without global privileges on a schema
 * but with grants only on some tables can't connect if the default schema is specified
 * in the connection string")
 *
 * - Connect using different default databases with database and table level grants.
 */


#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>

void run_test(TestConnections* Test, const char* database)
{

    Test->reset_timeout();
    Test->tprintf("Trying to connect using 'table_privilege'@'%%' to database '%s'", database);

    MYSQL* conn = open_conn_db(Test->maxscale->rwsplit_port,
                               Test->maxscale->ip4(),
                               database,
                               "table_privilege",
                               "pass",
                               Test->maxscale_ssl);

    if (conn && mysql_errno(conn) == 0)
    {
        Test->reset_timeout();
        Test->tprintf("Trying SELECT on %s.t1", database);
        Test->try_query(conn, "SELECT * FROM t1");
    }
    else
    {
        Test->add_result(1, "Failed to connect using database '%s': %s", database, mysql_error(conn));
    }

    mysql_close(conn);
}

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);

    Test->maxscale->connect_maxscale();
    Test->tprintf("Preparing test");
    Test->reset_timeout();
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE IF EXISTS db1");
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE IF EXISTS db2");
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE IF EXISTS db3");
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE IF EXISTS db4");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE DATABASE db1");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE DATABASE db2");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE DATABASE db3");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE DATABASE db4");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE TABLE db1.t1 (id INT)");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE TABLE db2.t1 (id INT)");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE TABLE db3.t1 (id INT)");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE TABLE db4.t1 (id INT)");
    execute_query(Test->maxscale->conn_rwsplit, "INSERT INTO db1.t1  VALUES (1)");
    execute_query(Test->maxscale->conn_rwsplit, "INSERT INTO db2.t1  VALUES (1)");
    execute_query(Test->maxscale->conn_rwsplit, "INSERT INTO db3.t1  VALUES (1)");
    execute_query(Test->maxscale->conn_rwsplit, "INSERT INTO db4.t1  VALUES (1)");
    execute_query(Test->maxscale->conn_rwsplit,
                  "CREATE USER 'table_privilege'@'%%' IDENTIFIED BY 'pass'");
    execute_query(Test->maxscale->conn_rwsplit, "GRANT SELECT ON db1.* TO 'table_privilege'@'%%'");
    execute_query(Test->maxscale->conn_rwsplit, "GRANT SELECT ON db2.* TO 'table_privilege'@'%%'");
    execute_query(Test->maxscale->conn_rwsplit, "GRANT SELECT ON db3.t1 TO 'table_privilege'@'%%'");
    execute_query(Test->maxscale->conn_rwsplit, "GRANT SELECT ON db4.t1 TO 'table_privilege'@'%%'");

    Test->repl->sync_slaves();

    run_test(Test, "db1");
    run_test(Test, "db2");
    run_test(Test, "db3");
    run_test(Test, "db4");

    Test->tprintf("Cleaning up...");
    Test->reset_timeout();
    Test->maxscale->connect_maxscale();
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE db1");
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE db2");
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE db3");
    execute_query(Test->maxscale->conn_rwsplit, "DROP DATABASE db4");
    execute_query(Test->maxscale->conn_rwsplit, "DROP USER 'table_privilege'@'%%'");

    int rval = Test->global_result;
    delete Test;
    return rval;
}
