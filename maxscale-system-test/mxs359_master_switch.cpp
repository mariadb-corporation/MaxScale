/**
 * MXS-359: Switch master mid-session
 *
 * https://jira.mariadb.org/browse/MXS-359
 */
#include "testconnections.h"

TestConnections* global_test;

void change_master(int next, int current)
{
    TestConnections& test = *global_test;
    test.repl->connect();
    test.repl->change_master(current, next);
    test.repl->close_connections();
}

struct Test
{
    const char* query;
    bool        should_work;

    Test(const char* q = NULL, bool works = true):
        query(q),
        should_work(works)
    {
    }
};

void do_test(Test pre, Test post)
{
    TestConnections& test = *global_test;
    int rc;
    test.maxscales->connect();

    if (pre.query)
    {
        rc = execute_query_silent(test.maxscales->conn_rwsplit[0], pre.query);
        test.assert((rc == 0) == pre.should_work, "Expected query '%s' to %s: %s",
                    pre.query, pre.should_work ? "succeed" : "fail",
                    mysql_error(test.maxscales->conn_rwsplit[0]));
    }

    change_master(1, 0);
    sleep(5);

    rc = execute_query_silent(test.maxscales->conn_rwsplit[0], post.query);
    test.assert((rc == 0) == post.should_work, "Expected query '%s' to %s: %s",
                post.query, post.should_work ? "succeed" : "fail",
                mysql_error(test.maxscales->conn_rwsplit[0]));

    change_master(0, 1);
    test.maxscales->disconnect();

    sleep(5);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    global_test = &test;

    // Prepare a table for testing
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.repl->sync_slaves();
    test.maxscales->disconnect();

    test.tprintf("Check that write after change works");
    do_test({}, {"INSERT INTO test.t1 VALUES (1)"});

    test.tprintf("Check that write with open transaction fails");
    do_test({"START TRANSACTION"}, {"INSERT INTO test.t1 VALUES (1)", false});

    test.tprintf("Check that read with open read-only transaction works");
    do_test({"START TRANSACTION READ ONLY"}, {"SELECT 1"});

    test.tprintf("Check that write with autocommit=0 fails");
    do_test({"SET autocommit=0"}, {"INSERT INTO test.t1 VALUES (1)", false});

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");
    test.repl->fix_replication();
    test.maxscales->disconnect();

    return test.global_result;
}
