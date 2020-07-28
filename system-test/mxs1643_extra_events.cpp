/**
 * MXS-1643: Too many monitor events are triggered
 *
 * https://jira.mariadb.org/browse/MXS-1643
 */
#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // Check that master gets the slave status when set into read-only mode
    test.tprintf("Set master into read-only mode");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "SET GLOBAL read_only=ON");
    test.maxscales->wait_for_monitor();
    test.tprintf("Check that the current master now has the slave label");
    test.log_excludes(0, "server1.*\\[Master, Running\\] -> \\[Running\\]");
    test.log_includes(0, "server1.*\\[Master, Running\\] -> \\[Slave, Running\\]");
    test.maxscales->ssh_node_f(0, true, "truncate -s 0 /var/log/maxscale/maxscale.log");

    // Check that the Master and Slave status aren't both set
    execute_query(test.repl->nodes[0], "SET GLOBAL read_only=OFF");
    test.maxscales->wait_for_monitor();
    test.tprintf("Check that the new master doesn't have both slave and master labels");
    test.log_excludes(0, "server1.*\\[Slave, Running\\] -> \\[Master, Slave, Running\\]");
    test.log_excludes(0, "server1.*\\[Running\\] -> \\[Master, Running\\]");
    test.log_includes(0, "server1.*\\[Slave, Running\\] -> \\[Master, Running\\]");

    return test.global_result;
}
