/**
 * MXS-1503: Test master reconnection with session command history
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

    auto check_result = [&test](std::string name, std::string res) {
            std::string query = "SELECT " + name;
            char value[1024];
            return find_field(test.maxscales->conn_rwsplit[0], query.c_str(), name.c_str(), value) == 0
                   && res == value;
        };

    test.maxscales->connect();
    test.expect(query("DROP TABLE IF EXISTS test.t1;") == 0, "DROP TABLE should work.");
    test.expect(query("CREATE TABLE test.t1 (id INT);") == 0, "CREATE TABLE should work.");

    // Execute session commands so that the history is not empty
    cout << "Setting user variables" << endl;
    test.expect(query("SET @a = 1") == 0, "First session command should work.");
    test.expect(query("USE test") == 0, "Second session command should work.");
    test.expect(query("SET @b = 2") == 0, "Third session command should work.");

    // Block the master to trigger reconnection
    cout << "Blocking master" << endl;
    test.repl->block_node(0);
    sleep(10);
    cout << "Unblocking master" << endl;
    test.repl->unblock_node(0);
    sleep(10);

    // Check that inserts work
    cout << "Selecting user variables" << endl;
    test.set_timeout(15);
    test.expect(query("INSERT INTO test.t1 VALUES (1)") == 0, "Write should work after unblocking master");
    test.expect(check_result("@a", "1"), "@a should be 1");
    test.expect(check_result("@b", "2"), "@b should be 2");
    query("DROP TABLE test.t1");

    return test.global_result;
}
