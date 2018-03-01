/**
 * Readwritesplit multi-statment test
 *
 * - Configure strict multi-statement mode
 * - Execute multi-statment query
 * - All queries should go to the master
 * - Configure for relaxed multi-statement mode
 * - Execute multi-statment query
 * - Only the multi-statement query should go to the master
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
    test.tprintf("Configuration: strict_multi_stmt=true");

    test.add_result(execute_query_check_one(test.conn_rwsplit,
                                            "SELECT @@server_id",
                                            slave_id),
                    "Query should be routed to slave");

    test.add_result(execute_query_check_one(test.conn_rwsplit,
                                            "USE test; SELECT @@server_id",
                                            master_id),
                    "Query should be routed to master");

    test.add_result(execute_query_check_one(test.conn_rwsplit,
                                            "SELECT @@server_id",
                                            master_id),
                    "All queries should be routed to master");

    test.close_maxscale_connections();

    // Reconfigure MaxScale
    test.ssh_maxscale(true, "sed -i 's/strict_multi_stmt=true/strict_multi_stmt=false/' /etc/maxscale.cnf");
    test.restart_maxscale();

    test.connect_maxscale();
    test.tprintf("Configuration: strict_multi_stmt=false");

    test.add_result(execute_query_check_one(test.conn_rwsplit,
                                            "SELECT @@server_id",
                                            slave_id),
                    "Query should be routed to slave");

    test.add_result(execute_query_check_one(test.conn_rwsplit,
                                            "USE test; SELECT @@server_id",
                                            master_id),
                    "Query should be routed to master");

    test.add_result(execute_query_check_one(test.conn_rwsplit,
                                            "SELECT @@server_id",
                                            slave_id),
                    "Query should be routed to slave");

    test.close_maxscale_connections();

    return test.global_result;
}
