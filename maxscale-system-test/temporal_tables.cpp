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

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    test.connect_maxscale();

    test.tprintf("Create a table and insert two rows into it");
    test.set_timeout(30);

    execute_query(test.conn_rwsplit, "USE test");
    create_t1(test.conn_rwsplit);
    execute_query(test.conn_rwsplit, "INSERT INTO t1 (x1, fl) VALUES(0, 1)");
    execute_query(test.conn_rwsplit, "INSERT INTO t1 (x1, fl) VALUES(1, 1)");

    test.tprintf("Create temporary table and insert one row");
    test.set_timeout(30);

    execute_query(test.conn_rwsplit, "create temporary table t1 as (SELECT * FROM t1 WHERE fl=3)");
    execute_query(test.conn_rwsplit, "INSERT INTO t1 (x1, fl) VALUES(0, 1)");

    test.tprintf("Check that the temporary table has one row");
    test.set_timeout(90);

    test.add_result(execute_select_query_and_check(test.conn_rwsplit, "SELECT * FROM t1", 1),
                    "Current connection should show one row");
    test.add_result(execute_select_query_and_check(test.conn_master, "SELECT * FROM t1", 2),
                    "New connection should show two rows");
    test.add_result(execute_select_query_and_check(test.conn_slave, "SELECT * FROM t1", 2),
                    "New connection should show two rows");

    printf("Drop temporary table and check that the real table has two rows");
    test.set_timeout(90);

    execute_query(test.conn_rwsplit, "DROP TABLE t1");
    test.add_result(execute_select_query_and_check(test.conn_rwsplit, "SELECT * FROM t1", 2),
                    "check failed");
    test.add_result(execute_select_query_and_check(test.conn_master, "SELECT * FROM t1", 2),
                    "check failed");
    test.add_result(execute_select_query_and_check(test.conn_slave, "SELECT * FROM t1", 2),
                    "check failed");

    test.close_maxscale_connections();

    return test.global_result;
}
