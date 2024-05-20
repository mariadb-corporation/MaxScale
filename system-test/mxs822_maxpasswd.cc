/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file mxs822_maxpasswd.cpp Regression test for bug MXS-822 ("encrypted passwords containing special
 * characters appear to not work")
 * - create .secret with maxkeys
 * - generate encrypted password with maxpasswd, use password with special characters
 * - replace passwords in maxscale.cnf with generated encrypted password
 * - try to connect to RWSplit
 * - repeate for several other password with special characters
 */

#include <maxtest/testconnections.hh>

void try_password(TestConnections& test, Connection& m, const std::string& pass,
                  const std::string& secretsdir = "")
{
    /**
     * Create the user
     */
    m.query("CREATE USER 'test'@'%' IDENTIFIED BY '" + pass + "'");
    m.query("GRANT ALL ON *.* TO 'test'@'%'");

    /**
     * Encrypt and change the password
     */
    test.tprintf("Encrypting password: %s", pass.c_str());
    int rc = test.maxscale->ssh_node_f(true, "maxpasswd %s '%s' > /tmp/encrypted.txt",
                                       secretsdir.c_str(), pass.c_str());
    test.expect(rc == 0, "Encryption failed");

    auto encrypted = test.maxscale->ssh_output("cat /tmp/encrypted.txt").output;
    test.tprintf("Encrypted password: %s", encrypted.c_str());

    rc = test.maxscale->ssh_node_f(true, "maxpasswd %s -d %s > /tmp/decrypted.txt",
                                   secretsdir.c_str(), encrypted.c_str());
    test.expect(rc == 0, "Decryption failed");

    auto decrypted = test.maxscale->ssh_output("cat /tmp/decrypted.txt").output;
    test.tprintf("Decrypted password: %s", decrypted.c_str());
    test.expect(decrypted == pass, "Decrypted password should be identical");

    rc = test.maxscale->ssh_node_f(true,
                                   "sed -i 's/user=.*/user=test/' /etc/maxscale.cnf && "
                                   "sed -i 's/password=.*/password=%s/' /etc/maxscale.cnf",
                                   encrypted.c_str());

    test.expect(test.maxscale->restart() == 0, "Failed to start MaxScale");

    // Wait for a monitoring cycle to make sure that the connection creation works.
    test.maxscale->wait_for_monitor();

    auto rws = test.maxscale->rwsplit();
    test.expect(rws.connect(), "Connection failed: %s", rws.error());
    test.expect(rws.query("SELECT 1"), "Query failed: %s", rws.error());

    m.query("DROP USER 'test'@'%'");
}

void test_main(TestConnections& test)
{
    auto c = test.repl->get_connection(0);
    c.connect();

    test.maxscale->ssh_node_f(true, "maxkeys");

    try_password(test, c, "aaa$aaa");
    try_password(test, c, "#¤&");
    try_password(test, c, "пароль");

    test.maxscale->ssh_node_f(true, "sudo mv /var/lib/maxscale/.secrets /tmp/.secrets");
    test.maxscale->ssh_node_f(true, "sudo sed -i '/threads=/ a secretsdir=/tmp' /etc/maxscale.cnf");
    try_password(test, c, "hello world", "/tmp");
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    return TestConnections().run_test(argc, argv, test_main);
}
