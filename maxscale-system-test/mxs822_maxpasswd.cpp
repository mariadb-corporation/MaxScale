/**
 * @file mxs822_maxpasswd.cpp Regression test for bug MXS-822 ("encrypted passwords containing special characters appear to not work")
 * - create .secret with maxkeys
 * - generate encripted password with maxpasswd, use password with special characters
 * - replace passwords in maxscale.cnf with generated encripted password
 * - try to connect to RWSplit
 * - restore passwords in maxscale.cnf
 * - repeate for several other password with special characters
 */

#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

void try_password(TestConnections* Test, char * pass)
{

    /**
     * Create the user
     */
    Test->connect_maxscale();
    execute_query_silent(Test->maxscales->conn_rwsplit[0], "DROP USER 'test'@'%'");
    execute_query(Test->maxscales->conn_rwsplit[0], "CREATE USER 'test'@'%%' IDENTIFIED BY '%s'", pass);
    execute_query(Test->maxscales->conn_rwsplit[0], "GRANT ALL ON *.* TO 'test'@'%%'");
    Test->close_maxscale_connections();

    /**
     * Encrypt and change the password
     */
    Test->tprintf("Encrypting password: %s", pass);
    Test->set_timeout(30);
    int rc = Test->ssh_maxscale(true, "maxpasswd /var/lib/maxscale/ '%s' | tr -dc '[:xdigit:]' > /tmp/pw.txt && "
                                "sed -i 's/user=.*/user=test/' /etc/maxscale.cnf && "
                                "sed -i \"s/passwd=.*/passwd=$(cat /tmp/pw.txt)/\" /etc/maxscale.cnf && "
                                "service maxscale restart && "
                                "sleep 3 && "
                                "sed -i 's/user=.*/user=maxskysql/' /etc/maxscale.cnf && "
                                "sed -i 's/passwd=.*/passwd=skysql/' /etc/maxscale.cnf && "
                                "service maxscale restart", pass);

    Test->add_result(rc, "Failed to encrypt password '%s'", pass);
    sleep(3);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    Test->ssh_maxscale(true, "maxkeys");
    Test->ssh_maxscale(true, "sudo chown maxscale:maxscale /var/lib/maxscale/.secrets");

    try_password(Test, (char *) "aaa$aaa");
    try_password(Test, (char *) "#¤&");
    try_password(Test, (char *) "пароль");

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
