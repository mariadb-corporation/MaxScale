/**
 * @file encrypted_passwords.cpp - Test maxkeys and maxpasswd interaction with MaxScale
 */
#include <my_config.h>
#include <iostream>
#include "testconnections.h"

/** Remove old keys and create a new one */
int create_key(TestConnections *test)
{
    int res = 0;
    test->set_timeout(120);
    test->tprintf("Creating new encryption keys\n");
    test->ssh_maxscale(true, "test -f /var/lib/maxscale/.secrets && rm /var/lib/maxscale/.secrets");
    test->ssh_maxscale(true, "maxkeys");
    char *result = test->ssh_maxscale_output(false, "test -f /var/lib/maxscale/.secrets && echo SUCCESS");

    if (strcmp(result, "SUCCESS") != 0)
    {
        test->tprintf("FAILURE: /var/lib/maxscale/.secrets was not created\n");
        res = 1;
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
    test->tprintf("Encrypted password is: %s\n", enc_pw);
    test->ssh_maxscale(true, "sed -i -e 's/passwd=skysql/passwd=%s/' /etc/maxscale.cnf", enc_pw);
    free(enc_pw);

    test->tprintf("Starting MaxScale and waiting 15 seconds\n");
    test->start_maxscale();
    sleep(15);

    test->tprintf("Checking if MaxScale is alive\n");
    return test->check_maxscale_alive();
}



int main(int argc, char *argv[])
{
    TestConnections * test = new TestConnections(argc, argv);

    test->global_result += create_key(test);
    test->global_result += hash_password(test);

    test->copy_all_logs();
    return test->global_result;
}
