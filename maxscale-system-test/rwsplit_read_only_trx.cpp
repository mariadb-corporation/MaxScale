/**
 * Readwritesplit read-only transaction test
 *
 * - Check that read-only transactions are routed to slaves
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    char master_id[200];
    char slave_id[200];

    // Get the server IDs of the master and the slave
    test.repl->connect();
    sprintf(master_id, "%d", test.repl->get_server_id(0));
    sprintf(slave_id, "%d", test.repl->get_server_id(1));

    test.connect_maxscale();

    execute_query_silent(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");
    execute_query_silent(test.maxscales->conn_rwsplit[0], "CREATE TABLE test.t1(id int)");

    // Test read-only transaction with commit
    test.try_query(test.maxscales->conn_rwsplit[0], "START TRANSACTION READ ONLY");
    test.add_result(execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id", slave_id),
                    "Query should be routed to slave");
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");

    // Test read-only transaction with rollback
    test.try_query(test.maxscales->conn_rwsplit[0], "START TRANSACTION READ ONLY");
    test.add_result(execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id", slave_id),
                    "Query should be routed to slave");
    test.try_query(test.maxscales->conn_rwsplit[0], "ROLLBACK");

    // Test normal transaction
    test.try_query(test.maxscales->conn_rwsplit[0], "START TRANSACTION");
    test.add_result(execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id", master_id),
                    "Query should be routed to master");
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");

    // Test writes in read-only transaction
    test.try_query(test.maxscales->conn_rwsplit[0], "START TRANSACTION READ ONLY");
    test.add_result(execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id", slave_id),
                    "Query should be routed to slave");
    test.add_result(execute_query(test.maxscales->conn_rwsplit[0], "UPDATE test.t1 SET id=0") == 0,
                    "Query should fail");
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");


    test.close_maxscale_connections();

    return test.global_result;
}
