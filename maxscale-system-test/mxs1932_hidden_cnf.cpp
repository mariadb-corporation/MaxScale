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

    // Create a file with a guaranteed bad configuration (turbochargers are not yet supported)
    ofstream cnf("hidden.cnf");
    cnf << "[something]" << endl;
    cnf << "type=turbocharger" << endl;
    cnf << "target=maxscale" << endl;
    cnf << "speed=maximum" << endl;
    cnf.close();

    // Copy the configuration to MaxScale
    test.maxscales->copy_to_node_legacy("hidden.cnf", "~");

    // Move it into the maxscale.cnf.d directory and make it a hidden file
    test.maxscales->ssh_node_f(0, true,
                               "mkdir -p /etc/maxscale.cnf.d/;"
                               "mv %s/hidden.cnf /etc/maxscale.cnf.d/.hidden.cnf;"
                               "chown -R maxscale:maxscale /etc/maxscale.cnf.d/",
                               test.maxscales->access_homedir[0]);

    // Make sure the hidden configuration is not read and that MaxScale starts up
    test.assert(test.maxscales->restart_maxscale() == 0, "Starting MaxScale should succeed");

    test.maxscales->ssh_node_f(0, true, "rm -r /etc/maxscale.cnf.d/");
    remove("hidden.cnf");

    return test.global_result;
}
