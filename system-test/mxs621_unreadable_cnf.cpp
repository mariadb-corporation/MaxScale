/**
 * @file max621_unreadable_cnf.cpp mxs621 regression case ("MaxScale fails to start silently if config file is
 * not readable")
 *
 * - make maxscale.cnf unreadable
 * - try to restart Maxscale
 * - check log for error
 * - retore access rights to maxscale.cnf
 */


#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();
    Test->maxscale->ssh_node_f(true, "chmod 400 /etc/maxscale.cnf");
    Test->reset_timeout();
    Test->maxscale->restart_maxscale();
    Test->reset_timeout();
    int rv = Test->maxscale->ssh_node_f(true,
                                        "journalctl --since '-5s' -u maxscale | "
                                        "grep \"Opening file '/etc/maxscale.cnf' for reading failed\"");
    Test->expect(rv == 0,
                 "\"Opening file '/etc/maxscale.cnf' for reading failed\" not found in stderr output.");
    Test->reset_timeout();
    Test->maxscale->ssh_node_f(true, "chmod 777 /etc/maxscale.cnf");

    int rval = Test->global_result;
    delete Test;
    return rval;
}
