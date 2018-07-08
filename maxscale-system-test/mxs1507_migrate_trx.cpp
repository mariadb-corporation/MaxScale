/**
 * MXS-1507: Test migration of transactions
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
    string master = "server1";
    string slave = "server2";

    auto switchover = [&]()
    {
        test.maxscales->wait_for_monitor();
        int rc = test.maxscales->ssh_node_f(0, true, "maxctrl call command mariadbmon switchover MySQL-Monitor %s %s",
                                            slave.c_str(), master.c_str());
        test.assert(rc == 0, "Switchover should work");
        master.swap(slave);
        test.maxscales->wait_for_monitor();
    };

    auto query = [&](string q)
    {
        return execute_query_silent(test.maxscales->conn_rwsplit[0], q.c_str()) == 0;
    };

    auto ok = [&](string q)
    {
        test.assert(query(q), "Query '%s' should work: %s", q.c_str(), mysql_error(test.maxscales->conn_rwsplit[0]));
    };

    auto check = [&](string q)
    {
        ok("START TRANSACTION");
        Row row = get_row(test.maxscales->conn_rwsplit[0], q.c_str());
        ok("COMMIT");
        test.assert(!row.empty() && row[0] == "1", "Query should return 1: %s", q.c_str());
    };

    // Create a table, insert a value and make sure it's replicated to all slaves
    test.maxscales->connect();
    ok("CREATE OR REPLACE TABLE test.t1 (id INT)");
    ok("INSERT INTO test.t1 VALUES (1)");
    test.repl->connect();
    test.repl->sync_slaves();
    test.maxscales->disconnect();

    cout << "Commit transaction" << endl;
    test.maxscales->connect();
    ok("START TRANSACTION");
    ok("SELECT id FROM test.t1 WHERE id = 1 FOR UPDATE");
    switchover();
    ok("UPDATE test.t1 SET id = 2 WHERE id = 1");
    ok("COMMIT");
    check("SELECT COUNT(*) = 1 FROM t1 WHERE id = 2");

    test.maxscales->disconnect();

    cout << "Rollback transaction" << endl;
    test.maxscales->connect();
    ok("START TRANSACTION");
    ok("UPDATE test.t1 SET id = 1");
    switchover();
    ok("ROLLBACK");
    check("SELECT COUNT(*) = 1 FROM t1 WHERE id = 2");
    test.maxscales->disconnect();

    cout << "Read-only transaction" << endl;
    test.maxscales->connect();
    ok("START TRANSACTION READ ONLY");
    ok("SELECT @@server_id"); // This causes a checksum mismatch if the transaction is migrated
    switchover();
    ok("COMMIT");
    test.maxscales->disconnect();

    test.maxscales->connect();
    ok("DROP TABLE test.t1");
    test.maxscales->disconnect();

    // Even number of switchovers should bring us back to the original master
    switchover();

    return test.global_result;
}
