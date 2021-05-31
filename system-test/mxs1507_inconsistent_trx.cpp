/**
 * MXS-1507: Test inconsistent result detection
 *
 * https://jira.mariadb.org/browse/MXS-1507
 */
#include <maxtest/testconnections.hh>
#include <functional>
#include <iostream>
#include <vector>

using namespace std;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto query = [&](string q) {
            return execute_query_silent(test.maxscale->conn_rwsplit[0], q.c_str()) == 0;
        };

    auto ok = [&](string q) {
            test.expect(query(q),
                        "Query '%s' should work: %s",
                        q.c_str(),
                        mysql_error(test.maxscale->conn_rwsplit[0]));
        };

    auto err = [&](string q) {
            test.expect(!query(q), "Query should not work: %s", q.c_str());
        };

    // Create a table and insert one value
    test.maxscale->connect_rwsplit();
    ok("CREATE OR REPLACE TABLE test.t1 (id INT)");
    ok("INSERT INTO test.t1 VALUES (1)");

    // Make sure it's replicated to all slaves before starting the transaction
    test.repl->connect();
    test.repl->sync_slaves();

    // Read the inserted value inside a read-only transaction
    ok("START TRANSACTION READ ONLY");
    ok("SELECT * FROM test.t1");

    // Modify the related data mid-transaction
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (2)");
    test.repl->sync_slaves();
    test.repl->disconnect();

    // Block the node where the transaction is active
    test.repl->block_node(1);
    sleep(5);

    // The checksums for the results should conflict causing the replay to fail
    err("COMMIT");
    test.maxscale->disconnect();

    test.maxscale->connect_rwsplit();
    ok("DROP TABLE test.t1");
    test.maxscale->disconnect();

    return test.global_result;
}
