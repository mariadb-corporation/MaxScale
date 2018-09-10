/**
 * MXS-1889: generated [maxscale] section causes errors
 *
 * https://jira.mariadb.org/browse/MXS-1899
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscales->ssh_node_f(0, true, "maxctrl alter maxscale auth_connect_timeout 10");
    test.expect(test.maxscales->restart() == 0,
                "Restarting MaxScale after modification "
                "of global parameters should work");

    return test.global_result;
}
