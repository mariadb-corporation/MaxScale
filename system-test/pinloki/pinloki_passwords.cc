#include <maxtest/testconnections.hh>
#include "pinloki_select_master.hh"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    // Create new encryption keys
    auto rv = test.maxscale->ssh_output("maxkeys");
    test.expect(rv.rc == 0, "maxkeys failed: %s", rv.output.c_str());

    // Encrypt the password
    rv = test.maxscale->ssh_output("maxpasswd skysql");
    test.expect(rv.rc == 0, "maxpasswd failed: %s", rv.output.c_str());

    // Replace the passwords with the encrypted ones
    test.maxscale->ssh_output(
        "sed -i 's/password=wrong_password/password=" + rv.output + "/' /etc/maxscale.cnf");
    test.maxscale->start();
    test.maxscale->wait_for_monitor(2);

    return MasterSelectTest(test).result();
}
