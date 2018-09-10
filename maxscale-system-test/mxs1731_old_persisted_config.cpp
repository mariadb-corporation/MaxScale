/**
 * MXS-1731: Empty version_string is not detected
 *
 * https://jira.mariadb.org/browse/MXS-1731
 */

#include "testconnections.h"
#include <fstream>
#include <iostream>

using std::cout;
using std::endl;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    const char* filename = "/tmp/RW-Split-Router.cnf";

    {
        std::ofstream cnf(filename);
        cnf << "[RW-Split-Router]" << endl
            << "type=service" << endl
            << "version_string=" << endl;
    }

    test.maxscales->copy_to_node_legacy(filename, filename);
    test.maxscales->ssh_node_f(0, true,
                               "mkdir -p /var/lib/maxscale/maxscale.cnf.d/;"
                               "chown maxscale:maxscale /var/lib/maxscale/maxscale.cnf.d/;"
                               "cp %s /var/lib/maxscale/maxscale.cnf.d/RW-Split-Router.cnf", filename);

    test.maxscales->restart();
    test.check_maxscale_alive();

    int rc = test.maxscales->ssh_node_f(0, true, "grep 'version_string' /var/lib/maxscale/maxscale.cnf.d/RW-Split-Router.cnf");
    test.expect(rc == 0, "Generated configuration should have version_string defined and MaxScale should ignore it.");

    test.maxscales->ssh_node_f(0, true, "maxadmin alter service RW-Split-Router enable_root_user=false");

    test.maxscales->restart();
    test.check_maxscale_alive();

    rc = test.maxscales->ssh_node_f(0, true, "grep 'version_string' /var/lib/maxscale/maxscale.cnf.d/RW-Split-Router.cnf");
    test.expect(rc != 0, "Generated configuration should not have version_string defined.");

    return test.global_result;
}
