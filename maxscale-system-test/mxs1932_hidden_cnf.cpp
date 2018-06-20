/**
 * MXS-1932: Hidden files are not ignored
 *
 * https://jira.mariadb.org/browse/MXS-1932
 */

#include "testconnections.h"

#include <fstream>
#include <iostream>

using namespace std;

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    ofstream cnf("hidden.cnf");
    cnf << "[something]" << endl;
    cnf << "type=turbocharger" << endl;
    cnf << "target=maxscale" << endl;
    cnf << "speed=maximum" << endl;
    cnf.close();

    test.maxscales->copy_to_node_legacy("hidden.cnf", "~");
    test.maxscales->ssh_node_f(0, true,
                               "mkdir -p /etc/maxscale.cnf.d/;"
                               "mv %s/hidden.cnf /etc/maxscale.cnf.d/.hidden.cnf;"
                               "chown -R maxscale:maxscale /etc/maxscale.cnf.d/",
                               test.maxscales->access_homedir[0]);

    test.assert(test.maxscales->restart_maxscale() == 0, "Starting MaxScale should suceed");

    test.maxscales->ssh_node_f(0, true, "rm -r /etc/maxscale.cnf.d/");
    remove("hidden.cnf");

    return test.global_result;
}
