/**
 * MXS-359: Starting sessions without master
 *
 * https://jira.mariadb.org/browse/MXS-359
 */
#include "testconnections.h"
#include <vector>
#include <iostream>
#include <sstream>

using std::cout;
using std::endl;

struct TestCase
{
    const char* description;
    void        (* func)(TestConnections&, std::ostream&);
};

TestConnections* global_test;

void change_master(int next, int current)
{
    TestConnections& test = *global_test;
    test.maxscales->ssh_node_f(0, true, "maxadmin shutdown monitor MySQL-Monitor");
    test.repl->connect();
    test.repl->change_master(next, current);
    test.repl->close_connections();
    test.maxscales->ssh_node_f(0, true, "maxadmin restart monitor MySQL-Monitor");
}

void test_replaced_master(TestConnections& test, std::ostream& out)
{
    out << "Sanity check that reads and writes work" << endl;
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");

    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    out << "Reads should still work even if no master is available" << endl;
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");

    test.repl->unblock_node(0);
    change_master(1, 0);
    test.maxscales->wait_for_monitor();

    out << "Reads and writes after master change should work" << endl;
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (2)");
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");

    test.maxscales->disconnect();
    change_master(0, 1);
}

void test_new_master(TestConnections& test, std::ostream& out)
{
    out << "Block the master before connecting" << endl;
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    out << "Connect and check that read-only mode works" << endl;
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");

    change_master(1, 0);
    test.maxscales->wait_for_monitor(2);

    out << "Both reads and writes after master change should work" << endl;
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (2)");
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");

    test.repl->unblock_node(0);
    test.maxscales->disconnect();
    change_master(0, 1);
}

void test_master_failure(TestConnections& test, std::ostream& out)
{
    out << "Sanity check that reads and writes work" << endl;
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");

    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    out << "Reads should still work even if no master is available" << endl;
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");

    out << "Writes should fail" << endl;
    int rc = execute_query_silent(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test.expect(rc != 0, "Write after master failure should fail");

    test.repl->unblock_node(0);
    test.maxscales->disconnect();
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    global_test = &test;

    std::vector<TestCase> tests(
    {
        {"test_replaced_master", test_replaced_master},
        {"test_new_master", test_new_master},
        {"test_master_failure", test_master_failure}
    });

    // Create a table for testing
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.repl->sync_slaves();
    test.maxscales->disconnect();

    for (auto& i : tests)
    {
        std::stringstream out;
        test.tprintf("Running test: %s", i.description);
        i.func(test, out);
        if (test.global_result)
        {
            test.tprintf("Test '%s' failed: \n\n%s\n\n", i.description, out.str().c_str());
            break;
        }
    }

    // Wait for the monitoring to stabilize before dropping the table
    sleep(5);

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");
    test.repl->fix_replication();
    test.maxscales->disconnect();

    return test.global_result;
}
