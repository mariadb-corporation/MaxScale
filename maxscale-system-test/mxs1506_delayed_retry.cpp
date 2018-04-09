/**
 * MXS-1506: Delayed query retry
 *
 * https://jira.mariadb.org/browse/MXS-1506
 */
#include "testconnections.h"
#include <functional>
#include <thread>
#include <iostream>
#include <vector>

using namespace std;

struct TestCase
{
    string description;
    function<void ()> pre; // Called before master goes down
    function<void ()> main; // Called after master goes down
    function<void ()> check; // Called after connection is closed
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto query = [&test](string q)
    {
        return execute_query_silent(test.maxscales->conn_rwsplit[0], q.c_str()) == 0;
    };

    auto check = [&test](string q, string res)
    {
        test.repl->sync_slaves();
        test.maxscales->connect();
        auto rc = execute_query_check_one(test.maxscales->conn_rwsplit[0], q.c_str(), res.c_str()) == 0;
        test.maxscales->disconnect();
        test.assert(rc, "Query '%s' did not produce result of '%s'", q.c_str(), res.c_str());
    };

    auto ok = [&test, &query](string q)
    {
        test.assert(query(q), "Query '%' should work: %s", q.c_str(), mysql_error(test.maxscales->conn_rwsplit[0]));
    };

    auto err = [&test, &query](string q)
    {
        test.assert(!query(q), "Query should fail: %s", q.c_str());
    };

    auto block_master = [&test]()
    {
        test.repl->block_node(0);
        sleep(10);
        test.repl->unblock_node(0);
    };

    vector<TestCase> tests(
    {
        {
            "Test autocommit insert with master disconnection",
            bind(ok, "SELECT 1"),
            bind(ok, "INSERT INTO test.t1 VALUES (1)"),
            bind(check, "SELECT COUNT(*) FROM test.t1 WHERE id = 1", "1")
        },
        {
            "Test user variables in insert with master disconnection",
            bind(ok, "SET @a = 2"),
            bind(ok, "INSERT INTO test.t1 VALUES (@a)"),
            bind(check, "SELECT COUNT(*) FROM test.t1 WHERE id = 2", "1")
        },
        {
            "Check that writes in transactions aren't retried",
            bind(ok, "START TRANSACTION"),
            bind(err, "INSERT INTO test.t1 VALUES (3)"),
            bind(check, "SELECT COUNT(*) FROM test.t1 WHERE id = 3", "0")
        },
        {
            "Check that writes without autocommit aren't retried",
            bind(ok, "SET autocommit=0"),
            bind(err, "INSERT INTO test.t1 VALUES (4)"),
            bind(check, "SELECT COUNT(*) FROM test.t1 WHERE id = 4", "0")
        }
    });

    cout << "Create table for testing" << endl;
    test.maxscales->connect();
    ok("DROP TABLE IF EXISTS test.t1");
    ok("CREATE TABLE test.t1 (id INT)");
    test.maxscales->disconnect();

    for (auto a : tests)
    {
        cout << a.description << endl;
        test.maxscales->connect();
        a.pre();
        thread thr(block_master);
        sleep(5);
        a.main();
        test.maxscales->disconnect();
        thr.join();
        a.check();
    }

    test.maxscales->connect();
    query("DROP TABLE test.t1");
    test.maxscales->disconnect();

    return test.global_result;
}
