/**
 * @file encrypted_passwords.cpp - Test maxkeys and maxpasswd interaction with MaxScale
 * - put encrypted password into maxscale.cnf and try to use Maxscale
 */

#include <iostream>
#include <maxtest/testconnections.hh>

/** Remove old keys and create a new one */
int create_key(TestConnections* test)
{
    int res = 0;
    test->tprintf("Creating new encryption keys\n");
    test->maxscale->ssh_node(
        "test -f /var/lib/maxscale/.secrets && sudo rm /var/lib/maxscale/.secrets",
        true);
    test->maxscale->ssh_node("maxkeys", true);
    auto result = test->maxscale->ssh_output("sudo test -f /var/lib/maxscale/.secrets && echo SUCCESS",
                                             false);

    if (strncmp(result.output.c_str(), "SUCCESS", 7) != 0)
    {
        test->tprintf("FAILURE: /var/lib/maxscale/.secrets was not created\n");
        res = 1;
    }
    else
    {
        test->maxscale->ssh_node("sudo chown maxscale:maxscale /var/lib/maxscale/.secrets", true);
    }
    return res;
}


/** Hash a new password and start MaxScale */
int hash_password(TestConnections* test)
{
    test->maxscale->stop();

    test->tprintf("Creating a new encrypted password\n");
    auto res = test->maxscale->ssh_output("maxpasswd /var/lib/maxscale/ skysql");

    std::string enc_pw = res.output;
    auto pos = enc_pw.find('\n');
    if (pos != std::string::npos)
    {
        enc_pw = enc_pw.substr(0, pos);
    }

    test->tprintf("Encrypted password is: %s\n", enc_pw.c_str());
    test->maxscale->ssh_node_f(0, true,
                               "sed -i -e 's/password[[:space:]]*=[[:space:]]*skysql/password=%s/' /etc/maxscale.cnf",
                               enc_pw.c_str());

    test->tprintf("Starting MaxScale\n");
    test->maxscale->start_maxscale();

    test->tprintf("Checking if MaxScale is alive\n");
    return test->check_maxscale_alive();
}



int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);

    test->global_result += create_key(test);
    test->global_result += hash_password(test);

    int rval = test->global_result;
    delete test;
    return rval;
}
