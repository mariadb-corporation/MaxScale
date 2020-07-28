/**
 * MXS-1507: Test inconsistent result detection
 *
 * https://jira.mariadb.org/browse/MXS-1507
 */
#include "testconnections.h"
#include <functional>
#include <iostream>
#include <vector>

using namespace std;

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    auto query = [&](string q) {
            return execute_query_silent(test.maxscales->conn_rwsplit[0], q.c_str()) == 0;
        };

    auto ok = [&](string q) {
            test.expect(query(q),
                        "Query '%s' should work: %s",
                        q.c_str(),
                        mysql_error(test.maxscales->conn_rwsplit[0]));
        };

    auto kill_master = [&]() {
            test.repl->connect();
            int master = test.repl->find_master();
            test.repl->disconnect();
            test.repl->block_node(master);
            test.maxscales->wait_for_monitor(3);
            test.repl->unblock_node(master);
            test.maxscales->wait_for_monitor(3);
        };

    // Create a table
    test.maxscales->connect();
    ok("CREATE OR REPLACE TABLE test.t1 (id INT)");
    test.maxscales->disconnect();

    // Make sure it's replicated to all slaves before starting the transaction
    test.repl->connect();
    test.repl->sync_slaves();
    test.repl->disconnect();

    // Try to do a transaction across multiple master failures
    test.maxscales->connect();

    cout << "Start transaction, insert a value and read it" << endl;
    ok("START TRANSACTION");
    ok("INSERT INTO test.t1 VALUES (1)");
    ok("SELECT * FROM test.t1 WHERE id = 1");

    cout << "Killing master" << endl;
    kill_master();

    cout << "Insert value and read it" << endl;
    ok("INSERT INTO test.t1 VALUES (2)");
    ok("SELECT * FROM test.t1 WHERE id = 2");

    cout << "Killing second master" << endl;
    kill_master();

    cout << "Inserting value 3" << endl;
    ok("INSERT INTO test.t1 VALUES (3)");
    ok("SELECT * FROM test.t1 WHERE id = 3");

    cout << "Killing third master" << endl;
    kill_master();

    cout << "Selecting final result" << endl;
    ok("SELECT SUM(id) FROM test.t1");

    cout << "Killing fourth master" << endl;
    kill_master();

    cout << "Committing transaction" << endl;
    ok("COMMIT");
    test.maxscales->disconnect();

    test.maxscales->connect();
    cout << "Checking results" << endl;
    Row r = get_row(test.maxscales->conn_rwsplit[0], "SELECT SUM(id), @@last_insert_id FROM t1");
    test.expect(!r.empty() && r[0] == "6", "All rows were not inserted: %s",
                r.empty() ? "No rows" : r[0].c_str());
    test.maxscales->disconnect();

    test.maxscales->connect();
    ok("DROP TABLE test.t1");
    test.maxscales->disconnect();

    return test.global_result;
}
