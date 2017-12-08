/**
 * MXS-1468: Using dynamic commands to create readwritesplit configs fail after restart
 *
 * https://jira.mariadb.org/browse/MXS-1468
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.verbose = true;
    test.maxscales->ssh_node_f(0, true,
                               "maxadmin create monitor cluster-monitor mysqlmon;"
                               "maxadmin alter monitor cluster-monitor user=maxskysql password=skysql monitor_interval=1000;"
                               "maxadmin restart monitor cluster-monitor;"
                               "maxadmin create listener rwsplit-service rwsplit-listener 0.0.0.0 4006;"
                               "maxadmin create listener rwsplit-service rwsplit-listener2 0.0.0.0 4008;"
                               "maxadmin create listener rwsplit-service rwsplit-listener3 0.0.0.0 4009;"
                               "maxadmin list listeners;"
                               "maxadmin create server prod_mysql01 %s 3306;"
                               "maxadmin create server prod_mysql02 %s 3306;"
                               "maxadmin create server prod_mysql03 %s 3306;"
                               "maxadmin list servers;"
                               "maxadmin add server prod_mysql02 cluster-monitor rwsplit-service;"
                               "maxadmin add server prod_mysql01 cluster-monitor rwsplit-service;"
                               "maxadmin add server prod_mysql03 cluster-monitor rwsplit-service;"
                               "maxadmin list servers;", test.repl->IP[0], test.repl->IP[1], test.repl->IP[2]);
    test.verbose = false;

    test.tprintf("Restarting MaxScale");
    test.add_result(test.maxscales->restart_maxscale(0), "Restart should succeed");
    test.check_maxscale_alive(0);

    return test.global_result;
}
