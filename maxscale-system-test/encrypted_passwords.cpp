/**
 * @file encrypted_passwords.cpp - Test maxkeys and maxpasswd interaction with MaxScale
 * - put encrypted password into maxscale.cnf and try to use Maxscale
 */

#include <iostream>
#include "testconnections.h"

/** Remove old keys and create a new one */
int create_key(TestConnections *test)
{
    int res = 0;
    int exit_code;
    test->set_timeout(120);
    test->tprintf("Creating new encryption keys\n");
    test->maxscales->ssh_node(0, "test -f /var/lib/maxscale/.secrets && sudo rm /var/lib/maxscale/.secrets",
                              true);
    test->maxscales->ssh_node(0, "maxkeys", true);
    char *result = test->maxscales->ssh_node_output(0, "sudo test -f /var/lib/maxscale/.secrets && echo SUCCESS",
                                                    false, &exit_code);

    if (strncmp(result, "SUCCESS", 7) != 0)
    {
        test->tprintf("FAILURE: /var/lib/maxscale/.secrets was not created\n");
        res = 1;
    }
    else
    {
        test->maxscales->ssh_node(0, "sudo chown maxscale:maxscale /var/lib/maxscale/.secrets", true);
    }

    free(result);
    return res;
}


/** Hash a new password and start MaxScale */
int hash_password(TestConnections *test)
{
    test->maxscales->stop_maxscale(0);
    test->stop_timeout();

    int exit_code;
    test->tprintf("Creating a new encrypted password\n");
    char *enc_pw = test->maxscales->ssh_node_output(0, "maxpasswd /var/lib/maxscale/ skysql", true, &exit_code);

    char *ptr = strchr(enc_pw, '\n');
    if (ptr)
    {
        *ptr = '\0';
    }

    test->tprintf("Encrypted password is: %s\n", enc_pw);
    test->maxscales->ssh_node_f(0, true,
                                "sed -i -e 's/password[[:space:]]*=[[:space:]]*skysql/password=%s/' /etc/maxscale.cnf",
                                enc_pw);
    free(enc_pw);

    test->tprintf("Starting MaxScale\n");
    test->maxscales->start_maxscale(0);

    test->tprintf("Checking if MaxScale is alive\n");
    return test->check_maxscale_alive(0);
}



int main(int argc, char *argv[])
{
    TestConnections * test = new TestConnections(argc, argv);

    test->global_result += create_key(test);
    test->global_result += hash_password(test);

    int rval = test->global_result;
    delete test;
    return rval;
}
