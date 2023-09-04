/**
 * MXS-1889: generated [maxscale] section causes errors
 *
 * https://jira.mariadb.org/browse/MXS-1899
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->ssh_node_f(true, "maxctrl alter maxscale auth_connect_timeout 10");
    test.expect(test.maxscale->restart() == 0,
                "Restarting MaxScale after modification "
                "of global parameters should work");

    return test.global_result;
}
