/**
 * Test for MXS-1310.
 * - Only explicit databases used -> shard containing the explicit database
 * - Only implicit databases used -> shard containing current database
 * - Mix of explicit and implicit databases -> shard containing current database
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    // Get the @@server_id value from both shards
    char server_id[2][1024];
    test.repl->connect();
    sprintf(server_id[0], "%d", test.repl->get_server_id(0));
    sprintf(server_id[1], "%d", test.repl->get_server_id(1));
    execute_query(test.repl->nodes[0],
                  "CREATE DATABASE db1;"
                  "CREATE TABLE db1.t1(id int);"
                  "INSERT INTO db1.t1 VALUES (@@server_id)");
    execute_query(test.repl->nodes[1],
                  "CREATE DATABASE db2;"
                  "CREATE TABLE db2.t2(id int);"
                  "INSERT INTO db2.t2 VALUES (@@server_id)");
    test.repl->sync_slaves();

    test.tprintf("Run test with sharded database as active database");
    test.connect_rwsplit();
    test.try_query(test.maxscales->conn_rwsplit[0], "USE db2");
    execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, id FROM t2", server_id[1]);
    execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, id FROM db1.t1", server_id[0]);
    execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, a.id FROM t2 as a JOIN db1.t1 as b",
                            server_id[1]);
    test.close_rwsplit();

    test.tprintf("Run test with a common database as active database");
    test.connect_rwsplit();
    test.try_query(test.maxscales->conn_rwsplit[0], "USE db1");
    execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, id FROM t1", server_id[0]);
    execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, id FROM db2.t2", server_id[1]);
    execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @@server_id, a.id FROM t1 as a JOIN db1.t1 as b",
                            server_id[0]);
    test.close_rwsplit();

    //  Cleanup
    execute_query(test.repl->nodes[0], "DROP DATABASE db1");
    execute_query(test.repl->nodes[1], "DROP DATABASE db2");

    test.repl->fix_replication();

    return test.global_result;
}
