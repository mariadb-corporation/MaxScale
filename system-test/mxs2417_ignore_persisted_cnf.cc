/**
 * MXS-2417: Ignore persisted configs with load_persisted_configs=false
 * https://jira.mariadb.org/browse/MXS-2417
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Creating a server and verifying it exists");
    test.check_maxctrl("create server server1234 127.0.0.1 3306");
    test.check_maxctrl("show server server1234");

    test.tprintf("Restarting MaxScale");
    test.maxscale->restart_maxscale();

    test.tprintf("Creating the server again and verifying it is successful");
    test.check_maxctrl("create server server1234 127.0.0.1 3306");
    test.check_maxctrl("show server server1234");

    return test.global_result;
}
