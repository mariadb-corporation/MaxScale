/**
 * Minimal MaxCtrl sanity check
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    int rc = test.maxscales->ssh_node_f(0, false, "maxctrl help list servers");
    test.assert(rc == 0, "`help list servers` should work");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl --tsv list servers|grep 'Master, Running'");
    test.assert(rc == 0, "`list servers` should return at least one row with: Master, Running");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl set server server1 maintenance");
    test.assert(rc == 0, "`set server` should work");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl --tsv list servers|grep 'Maintenance'");
    test.assert(rc == 0, "`list servers` should return at least one row with: Maintanance");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl clear server server1 maintenance");
    test.assert(rc == 0, "`clear server` should work");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl --tsv list servers|grep 'Maintenance'");
    test.assert(rc != 0, "`list servers` should have no rows with: Maintanance");

    test.check_maxscale_alive();
    return test.global_result;
}
