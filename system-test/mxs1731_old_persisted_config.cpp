/**
 * MXS-1731: Empty version_string is not detected
 *
 * https://jira.mariadb.org/browse/MXS-1731
 */

#include <maxtest/testconnections.hh>
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
            << "router=readwritesplit" << endl
            << "user=maxskysql" << endl
            << "password=skysql" << endl
            << "servers=server1" << endl
            << "version_string=" << endl;
    }

    test.maxscales->copy_to_node(filename, filename);
    test.maxscales->ssh_node_f(0,
                               true,
                               "mkdir -p /var/lib/maxscale/maxscale.cnf.d/;"
                               "chown maxscale:maxscale /var/lib/maxscale/maxscale.cnf.d/;"
                               "cp %s /var/lib/maxscale/maxscale.cnf.d/RW-Split-Router.cnf",
                               filename);
    test.maxscales->ssh_node_f(0,
                               true,
                               "chmod a+r /var/lib/maxscale/maxscale.cnf.d/RW-Split-Router.cnf");

    test.maxscales->restart();
    test.check_maxscale_alive();

    int rc = test.maxscales->ssh_node_f(0,
                                        true,
                                        "grep 'version_string' /var/lib/maxscale/maxscale.cnf.d/RW-Split-Router.cnf");
    test.expect(rc == 0,
                "Generated configuration should have version_string defined and MaxScale should ignore it.");

    test.check_maxctrl("alter service RW-Split-Router enable_root_user true");
    test.check_maxctrl("alter service RW-Split-Router enable_root_user false");

    test.maxscales->restart();
    test.check_maxscale_alive();

    rc = test.maxscales->ssh_node_f(0,
                                    true,
                                    "grep 'version_string' /var/lib/maxscale/maxscale.cnf.d/RW-Split-Router.cnf");
    test.expect(rc != 0, "Generated configuration should not have version_string defined.");

    return test.global_result;
}
