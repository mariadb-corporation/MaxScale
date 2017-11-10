/**
 * Run MaxCtrl test suite on the MaxScale machine
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    // Use galera_003 as the secondary MaxScale node
    TestConnections::set_secondary_maxscale("galera_003_network", "galera_003_network6");
    TestConnections test(argc, argv);

    // This is not very nice as it's a bit too intrusive
    system("envsubst < maxctrl_scripts.sh.in > maxctrl_scripts.sh");
    system("chmod +x maxctrl_scripts.sh");
    test.copy_to_maxscale("test_maxctrl.sh", "~");
    test.copy_to_maxscale("maxctrl_scripts.sh", "~");
    test.ssh_maxscale(true,"ssh-keygen -f maxscale_key -P \"\"");
    test.copy_from_maxscale((char*)"~/maxscale_key.pub", (char*)".");
    test.galera->copy_to_node("./maxscale_key.pub", "~", 3);
    test.galera->ssh_node(3, false, "cat ~/maxscale_key.pub >> ~/.ssh/authorized_keys;"
                          "sudo iptables -I INPUT -p tcp --dport 8989 -j ACCEPT;");

    // TODO: Don't handle test dependencies in tests
    test.tprintf("Installing NPM");
    test.ssh_maxscale(true,"yum -y install epel-release;yum -y install npm git;");

    test.tprintf("Starting test");
    test.verbose = true;
    int rv = test.ssh_maxscale(true, "export maxscale_access_homedir=%s; export maxscale2_API=%s:8989; ./test_maxctrl.sh",
                               test.maxscale_access_homedir, test.galera->IP[3]);
    test.verbose = false;

    test.tprintf("Removing NPM");
    test.ssh_maxscale(true, "yum -y remove npm epel-release");

    return rv;
}
