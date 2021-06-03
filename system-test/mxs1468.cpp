/**
 * MXS-1468: Using dynamic commands to create readwritesplit configs fail after restart
 *
 * https://jira.mariadb.org/browse/MXS-1468
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    std::vector<std::string> commands =
    {
        "create monitor cluster-monitor mysqlmon  user=maxskysql password=skysql monitor_interval=1000",
        "create listener rwsplit-service rwsplit-listener 4006",
        "create listener rwsplit-service rwsplit-listener2 4008",
        "create listener rwsplit-service rwsplit-listener3 4009",
        "list listeners rwsplit-service",
        "create server prod_mysql01 " + std::string(test.repl->ip4(0)) + " 3306",
        "create server prod_mysql02 " + std::string(test.repl->ip4(1)) + " 3306",
        "create server prod_mysql03 " + std::string(test.repl->ip4(2)) + " 3306",
        "list servers",
        "link service rwsplit-service prod_mysql02 prod_mysql01 prod_mysql03",
        "link monitor cluster-monitor prod_mysql02 prod_mysql01 prod_mysql03",
        "list servers",
    };

    for (auto a : commands)
    {
        test.check_maxctrl(a);
    }

    test.tprintf("Restarting MaxScale");
    test.add_result(test.maxscale->restart_maxscale(), "Restart should succeed");
    test.check_maxscale_alive();

    return test.global_result;
}
