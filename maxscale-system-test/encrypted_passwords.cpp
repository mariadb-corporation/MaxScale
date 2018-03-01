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
    test->set_timeout(120);
    test->tprintf("Creating new encryption keys\n");
    test->ssh_maxscale(true, "test -f /var/lib/maxscale/.secrets && sudo rm /var/lib/maxscale/.secrets");
    test->ssh_maxscale(true, "maxkeys");
    char *result = test->ssh_maxscale_output(false, "sudo test -f /var/lib/maxscale/.secrets && echo SUCCESS");

    if (strncmp(result, "SUCCESS", 7) != 0)
    {
        test->tprintf("FAILURE: /var/lib/maxscale/.secrets was not created\n");
        res = 1;
    }
    else
    {
        test->ssh_maxscale(true, "sudo chown maxscale:maxscale /var/lib/maxscale/.secrets");
    }

    free(result);
    return res;
}


/** Hash a new password and start MaxScale */
int hash_password(TestConnections *test)
{
    test->stop_maxscale();
    test->stop_timeout();

    int res = 0;
    test->tprintf("Creating a new encrypted password\n");
    char *enc_pw = test->ssh_maxscale_output(true, "maxpasswd /var/lib/maxscale/ skysql");

    char *ptr = strchr(enc_pw, '\n');
    if (ptr)
    {
        *ptr = '\0';
    }

    test->tprintf("Encrypted password is: %s\n", enc_pw);
    test->ssh_maxscale(true, "sed -i -e 's/passwd[[:space:]]*=[[:space:]]*skysql/passwd=%s/' /etc/maxscale.cnf",
                       enc_pw);
    free(enc_pw);

    test->tprintf("Starting MaxScale\n");
    test->start_maxscale();

    test->tprintf("Checking if MaxScale is alive\n");
    return test->check_maxscale_alive();
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
