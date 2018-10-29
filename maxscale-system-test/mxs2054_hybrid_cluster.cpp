/**
 * MXS-2054: Test "hybrid" clusters with namedserverfilter
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscales->ssh_node_f(0, true, "maxctrl set server server3 slave");
    test.maxscales->ssh_node_f(0, true, "maxctrl set server server4 slave");

    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE test.t1 AS SELECT 1 AS id");
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE test.t2 AS SELECT 2 AS id");
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE test.t3 AS SELECT 3 AS id");
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE test.t4 AS SELECT 4 AS id");
    test.repl->sync_slaves();
    test.repl->disconnect();

    test.maxscales->connect();

    Row server1 = get_row(test.maxscales->conn_rwsplit[0],
                          "SELECT @@server_id, @@last_insert_id, id FROM test.t1");
    Row server2 = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, id FROM test.t2");
    Row server3 = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, id FROM test.t3");
    Row server4 = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, id FROM test.t4");

    test.maxscales->disconnect();

    test.repl->connect();
    test.expect(server1[0] == test.repl->get_server_id_str(0),
                "First query without hint should go to server1, the master");
    test.expect(server2[0] == test.repl->get_server_id_str(1),
                "Second query without hint should go to server2, the slave");
    test.expect(server3[0] == test.repl->get_server_id_str(2),
                "First query with hint should go to server3, the first unmonitored server");
    test.expect(server4[0] == test.repl->get_server_id_str(3),
                "Second query with hint should go to server4, the second unmonitored server");
    test.repl->disconnect();

    return test.global_result;
}
