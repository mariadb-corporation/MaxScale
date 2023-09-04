/**
 * @file mxs822_maxpasswd.cpp Regression test for bug MXS-822 ("encrypted passwords containing special
 * characters appear to not work")
 * - create .secret with maxkeys
 * - generate encripted password with maxpasswd, use password with special characters
 * - replace passwords in maxscale.cnf with generated encripted password
 * - try to connect to RWSplit
 * - restore passwords in maxscale.cnf
 * - repeate for several other password with special characters
 */

#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

using namespace std;

void try_password(TestConnections* Test, char* pass)
{

    /**
     * Create the user
     */
    Test->maxscale->connect_maxscale();
    execute_query_silent(Test->maxscale->conn_rwsplit, "DROP USER 'test'@'%'");
    execute_query(Test->maxscale->conn_rwsplit, "CREATE USER 'test'@'%%' IDENTIFIED BY '%s'", pass);
    execute_query(Test->maxscale->conn_rwsplit, "GRANT ALL ON *.* TO 'test'@'%%'");
    Test->maxscale->close_maxscale_connections();

    /**
     * Encrypt and change the password
     */
    Test->tprintf("Encrypting password: %s", pass);
    Test->reset_timeout();
    int rc = Test->maxscale->ssh_node_f(true,
                                        "maxpasswd /var/lib/maxscale/ '%s' | tr -dc '[:xdigit:]' > /tmp/pw.txt && "
                                        "sed -i 's/user=.*/user=test/' /etc/maxscale.cnf && "
                                        "sed -i \"s/password=.*/password=$(cat /tmp/pw.txt)/\" /etc/maxscale.cnf && "
                                        "systemctl restart maxscale && "
                                        "sleep 3 && "
                                        "sed -i 's/user=.*/user=maxskysql/' /etc/maxscale.cnf && "
                                        "sed -i 's/password=.*/password=skysql/' /etc/maxscale.cnf && "
                                        "systemctl restart maxscale",
                                        pass);

    Test->add_result(rc, "Failed to encrypt password '%s'", pass);
    sleep(3);
}

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();

    Test->maxscale->ssh_node_f(true, "maxkeys");
    Test->maxscale->ssh_node_f(true, "sudo chown maxscale:maxscale /var/lib/maxscale/.secrets");

    try_password(Test, (char*) "aaa$aaa");
    try_password(Test, (char*) "#¤&");
    try_password(Test, (char*) "пароль");

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
