/**
 * Run MaxCtrl test suite on the MaxScale machine
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.copy_to_maxscale("test_maxctrl.sh", "~");

    // TODO: Don't handle test dependencies in tests
    test.tprintf("Installing NPM");
    test.ssh_maxscale(true,"yum -y install epel-release;yum -y install npm;");

    test.tprintf("Starting test");
    test.verbose = true;
    int rv = test.ssh_maxscale(false, "./test_maxctrl.sh");
    test.verbose = false;

    test.tprintf("Removing NPM");
    test.ssh_maxscale(true, "yum -y remove npm epel-release");

    return rv;
}
