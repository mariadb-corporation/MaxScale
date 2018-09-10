/**
 * MXS-1503: Testing of master_reconnection and master_failure_mode=error_on_write
 *
 * https://jira.mariadb.org/browse/MXS-1503
 */
#include "testconnections.h"
#include <vector>
#include <iostream>
#include <functional>

using std::cout;
using std::endl;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto query = [&test](std::string q) {
            return execute_query_silent(test.maxscales->conn_rwsplit[0], q.c_str());
        };

    auto error_matches = [&test](std::string q) {
            std::string err = mysql_error(test.maxscales->conn_rwsplit[0]);
            return err.find(q) != std::string::npos;
        };

    auto block_master = [&test]() {
            test.repl->block_node(0);
            sleep(10);
        };

    auto unblock_master = [&test]() {
            test.repl->unblock_node(0);
            sleep(10);
        };

    test.maxscales->connect();
    test.expect(query("DROP TABLE IF EXISTS test.t1") == 0,
                "DROP TABLE should work.");
    test.expect(query("CREATE TABLE test.t1 (id INT)") == 0,
                "CREATE TABLE should work.");
    test.expect(query("INSERT INTO test.t1 VALUES (1)") == 0,
                "Write should work at the start of the test.");

    block_master();
    test.expect(query("INSERT INTO test.t1 VALUES (1)") != 0,
                "Write should fail after master is blocked.");

    test.expect(error_matches("read-only"),
                "Error should mention read-only mode");

    unblock_master();
    test.expect(query("INSERT INTO test.t1 VALUES (1)") == 0,
                "Write should work after unblocking master");

    query("DROP TABLE test.t1");

    return test.global_result;
}
