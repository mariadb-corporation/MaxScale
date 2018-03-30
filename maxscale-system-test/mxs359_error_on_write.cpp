/**
 * MXS-359: Starting sessions without master
 *
 * https://jira.mariadb.org/browse/MXS-359
 */
#include "testconnections.h"
#include <vector>
#include <iostream>
#include <functional>

using std::cout;
using std::endl;

TestConnections* self;

static void change_master(int next, int current)
{
    TestConnections& test = *self;
    test.maxscales->ssh_node_f(0, true, "maxadmin shutdown monitor MySQL-Monitor");
    test.repl->connect();
    test.repl->change_master(next, current);
    test.repl->close_connections();
    test.maxscales->ssh_node_f(0, true, "maxadmin restart monitor MySQL-Monitor");
}

struct Query
{
    const char* query;
    bool        should_work;
};

typedef std::vector<Query> Queries;

typedef std::function<void()> Func;

struct Step
{
    const char* description;
    Func func;
    Queries queries;
};

struct TestCase
{
    const char* description;
    std::vector<Step> steps;
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    self = &test;

    Queries rw_ok({{"INSERT INTO test.t1 VALUES (1)", true}, {"SELECT * FROM test.t1", true}});
    Queries rw_err({{"INSERT INTO test.t1 VALUES (1)", false}, {"SELECT * FROM test.t1", true}});

    Func block_master = [&test]()
    {
        test.repl->block_node(0);
        sleep(10);
    };

    Func unblock_master = [&test]()
    {
        test.repl->unblock_node(0);
        sleep(10);
    };

    Func master_change = [&test]()
    {
        change_master(1, 0);
        sleep(10);
    };

    Func reset = [&test]()
    {
        test.repl->unblock_node(0);
        change_master(0, 1);
        sleep(10);
    };

    Func noop = []() {};

    std::vector<TestCase> tests(
    {
        {
            "Master failure and replacement",
            {
                {"Check that writes work at startup", noop, rw_ok},
                {"Block master and check that writes fail", block_master, rw_err},
                {"Change master and check that writes work", master_change, rw_ok},
                {"Reset cluster", reset, {}}
            }
        },
        {
            "No master on startup",
            {
                {"Block master and check that writes fail", block_master, rw_err},
                {"Unblock master and check that writes do not fail", unblock_master, rw_ok},
                {"Change master and check that writes work", master_change, rw_ok},
                {"Reset cluster", reset, {}}
            }
        }
    });

    // Create a table for testing
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.repl->sync_slaves();
    test.maxscales->disconnect();

    for (auto& i : tests)
    {
        test.tprintf("Running test: %s", i.description);
        test.maxscales->connect();

        for (auto t : i.steps)
        {
            cout << t.description << endl;
            t.func();
            for (auto q : t.queries)
            {
                int rc = execute_query_silent(test.maxscales->conn_rwsplit[0], q.query);
                test.assert(q.should_work == (rc == 0), "Step '%s': Query '%s' should %s: %s",
                            i.description, q.query, q.should_work ? "work" : "fail",
                            mysql_error(test.maxscales->conn_rwsplit[0]));
            }
        }

        if (test.global_result)
        {
            test.tprintf("Test '%s' failed", i.description);
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
