/**
 * MXS-1643: Too many monitor events are triggered
 *
 * https://jira.mariadb.org/browse/MXS-1643
 */
#include "testconnections.h"

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    // Check that master gets the slave status when set into read-only mode
    test.tprintf("Set master into read-only mode");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "SET GLOBAL read_only=ON");
    test.maxscales->wait_for_monitor();
    test.tprintf("Check that the current master now has the slave label");
    test.check_log_err(0, "[Master, Running] -> [Running]", false);
    test.check_log_err(0, "[Master, Running] -> [Slave, Running]", true);
    execute_query(test.repl->nodes[0], "SET GLOBAL read_only=OFF");
    test.maxscales->wait_for_monitor();
    test.maxscales->ssh_node_f(0, true, "truncate -s 0 /var/log/maxscale/maxscale.log");

    // Check that the Master and Slave status aren't both set
    test.tprintf("Block master and wait for monitor to detect it.");
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();
    test.tprintf("Check that the new master doesn't have both slave and master labels");
    test.check_log_err(0, "[Slave, Running] -> [Master, Slave, Running]", false);
    test.check_log_err(0, "[Slave, Running] -> [Master, Running]", true);
    test.repl->unblock_node(0);


    test.tprintf("Cleanup");
    test.repl->execute_query_all_nodes( "STOP ALL SLAVES; RESET SLAVE ALL;");
    test.repl->fix_replication();
    return test.global_result;
}
