/**
 * Check temporary tables commands functionality (relates to bug 430)
 *
 * - create t1 table and put some data into it
 * - create temporary table t1
 * - insert different data into t1
 * - check that SELECT FROM t1 gives data from temporary table
 * - create other connections using all MaxScale services and check that SELECT
 *   via these connections gives data from main t1, not temporary
 * - dropping temporary t1
 * - check that data from main t1 is not affected
 */

#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.maxscales->connect_maxscale(0);

    test.tprintf("Create a table and insert two rows into it");
    test.set_timeout(30);

    execute_query(test.maxscales->conn_rwsplit[0], "USE test");
    create_t1(test.maxscales->conn_rwsplit[0]);
    execute_query(test.maxscales->conn_rwsplit[0], "INSERT INTO t1 (x1, fl) VALUES(0, 1)");
    execute_query(test.maxscales->conn_rwsplit[0], "INSERT INTO t1 (x1, fl) VALUES(1, 1)");

    test.tprintf("Create temporary table and insert one row");
    test.set_timeout(30);

    execute_query(test.maxscales->conn_rwsplit[0],
                  "create temporary table t1 as (SELECT * FROM t1 WHERE fl=3)");
    execute_query(test.maxscales->conn_rwsplit[0], "INSERT INTO t1 (x1, fl) VALUES(0, 1)");

    test.tprintf("Check that the temporary table has one row");
    test.set_timeout(90);

    test.add_result(execute_select_query_and_check(test.maxscales->conn_rwsplit[0], "SELECT * FROM t1", 1),
                    "Current connection should show one row");
    test.add_result(execute_select_query_and_check(test.maxscales->conn_master[0], "SELECT * FROM t1", 2),
                    "New connection should show two rows");
    test.add_result(execute_select_query_and_check(test.maxscales->conn_slave[0], "SELECT * FROM t1", 2),
                    "New connection should show two rows");

    printf("Drop temporary table and check that the real table has two rows");
    test.set_timeout(90);

    execute_query(test.maxscales->conn_rwsplit[0], "DROP TABLE t1");
    test.add_result(execute_select_query_and_check(test.maxscales->conn_rwsplit[0], "SELECT * FROM t1", 2),
                    "check failed");
    test.add_result(execute_select_query_and_check(test.maxscales->conn_master[0], "SELECT * FROM t1", 2),
                    "check failed");
    test.add_result(execute_select_query_and_check(test.maxscales->conn_slave[0], "SELECT * FROM t1", 2),
                    "check failed");

    test.maxscales->close_maxscale_connections(0);

    // MXS-2103
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TEMPORARY TABLE temp.dummy5 (dum INT);");
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO temp.dummy5 VALUES(1),(2);");
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM temp.dummy5;");
    test.maxscales->disconnect();

    return test.global_result;
}
