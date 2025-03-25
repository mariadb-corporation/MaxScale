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
 * @file encrypted_passwords.cpp - Test maxkeys and maxpasswd interaction with MaxScale
 * - put encrypted password into maxscale.cnf and try to use Maxscale
 */

#include <iostream>
#include <maxtest/testconnections.hh>

/** Remove old keys and create a new one */
void create_key(TestConnections& test)
{
    int res = 0;
    test.tprintf("Creating new encryption keys");
    test.maxscale->ssh_node(
        "test -f /var/lib/maxscale/.secrets && sudo rm /var/lib/maxscale/.secrets",
        true);
    test.maxscale->ssh_node("maxkeys", true);
    auto result = test.maxscale->ssh_output("sudo test -f /var/lib/maxscale/.secrets && echo SUCCESS",
                                            false);

    test.expect(result.output == "SUCCESS", "/var/lib/maxscale/.secrets was not created");
    test.maxscale->ssh_node("sudo chown maxscale:maxscale /var/lib/maxscale/.secrets", true);
}


/** Hash a new password and start MaxScale */
void hash_password(TestConnections& test)
{
    test.maxscale->stop();

    test.tprintf("Creating a new encrypted password");
    auto res = test.maxscale->ssh_output("maxpasswd /var/lib/maxscale/ skysql");

    std::string enc_pw = res.output;
    auto pos = enc_pw.find('\n');
    if (pos != std::string::npos)
    {
        enc_pw = enc_pw.substr(0, pos);
    }

    test.tprintf("Encrypted password is: %s", enc_pw.c_str());
    test.maxscale->ssh_node_f(true,
                              "sed -i -e 's/password[[:space:]]*=[[:space:]]*skysql/password=%s/' /etc/maxscale.cnf",
                              enc_pw.c_str());

    test.tprintf("Starting MaxScale");
    test.maxscale->start_maxscale();

    test.tprintf("Checking if MaxScale is alive");
    test.expect(test.check_maxscale_alive() == 0, "MaxScale is not alive");
}

void encrypted_password_in_maxctrl(TestConnections& test)
{
    auto command = [&](std::string cmd, bool ok){
        int rc = test.maxscale->ssh_node_f(false, "%s", cmd.c_str());
        test.expect((rc == 0) == ok, "Command %s: %s", ok ? "failed" : "succeeded", cmd.c_str());
    };

    auto command_ok = [&](std::string cmd){
        command(cmd, true);
    };

    auto command_err = [&](std::string cmd){
        command(cmd, false);
    };

    test.tprintf("MXS-5449: Encrypted passwords in MaxCtrl");

    const char* user = test.maxscale->access_user();
    const char* secretsdir = test.maxscale->access_homedir();

    test.maxscale->ssh_node_f(true,
                              "cp /var/lib/maxscale/.secrets %s/.secrets;"
                              "chown %s %s/.secrets",
                              secretsdir, user, secretsdir);

    test.maxctrl("create user foobar foobar");
    auto enc = test.maxscale->ssh_output("maxpasswd "s + secretsdir + " foobar").output;

    test.maxscale->ssh_node_f(true,
                              "echo '[maxctrl]' > /tmp/maxctrl-plaintext.cnf;"
                              "echo 'user=foobar' >> /tmp/maxctrl-plaintext.cnf;"
                              "echo 'password=foobar' >> /tmp/maxctrl-plaintext.cnf;"
                              "echo '[maxctrl]' > /tmp/maxctrl-encrypted.cnf;"
                              "echo 'user=foobar' >> /tmp/maxctrl-encrypted.cnf;"
                              "echo 'password=%s' >> /tmp/maxctrl-encrypted.cnf;"
                              "echo 'secretsdir=%s' >> /tmp/maxctrl-encrypted.cnf;"
                              "chmod 0600 /tmp/maxctrl-plaintext.cnf /tmp/maxctrl-encrypted.cnf;"
                              "chown %s /tmp/maxctrl-plaintext.cnf /tmp/maxctrl-encrypted.cnf;",
                              enc.c_str(), secretsdir, user);

    command_ok("maxctrl --user=foobar --password=foobar list sessions");
    command_ok("maxctrl -c /tmp/maxctrl-plaintext.cnf list sessions");
    command_ok("sudo maxctrl --user=foobar --password=" + enc + " list sessions");
    command_ok("maxctrl --user=foobar --password=" + enc + " --secretsdir=" + secretsdir + " list sessions");
    command_ok(
        "echo " + enc + "|maxctrl --user=foobar --password='' --secretsdir=" + secretsdir + " list sessions");
    command_ok("maxctrl -c /tmp/maxctrl-encrypted.cnf list sessions");

    test.maxscale->ssh_node_f(true, "rm %s/.secrets", secretsdir);

    command_ok("maxctrl --user=foobar --password=foobar list sessions");
    command_ok("maxctrl -c /tmp/maxctrl-plaintext.cnf list sessions");
    command_ok("sudo maxctrl --user=foobar --password=" + enc + " list sessions");
    command_err("maxctrl --user=foobar --password=" + enc + " --secretsdir=" + secretsdir + " list sessions");
    command_err(
        "echo " + enc + "|maxctrl --user=foobar --password='' --secretsdir=" + secretsdir + " list sessions");
    command_err("maxctrl -c /tmp/maxctrl-encrypted.cnf list sessions");

    test.maxctrl("destroy user foobar");
    test.maxscale->ssh_node_f(true, "rm /tmp/maxctrl-plaintext.cnf /tmp/maxctrl-encrypted.cnf");
}

void mxs5520_no_password_reencryption(TestConnections& test)
{
    test.tprintf("MXS-5520: Passwords end up being re-encrypted when persisted");

    std::string config_path = "/var/lib/maxscale/maxscale.cnf.d/maxscale.cnf";
    auto res = test.maxscale->ssh_output("maxpasswd /var/lib/maxscale/ skysql");
    std::string original_pw = res.output;
    test.maxscale->stop();
    test.maxscale->ssh_node_f(true, "sed -i \"/maxscale/ a config_sync_password=%s\" /etc/maxscale.cnf",
                              res.output.c_str());

    test.maxscale->start();

    for (int i = 0; i < 5; i++)
    {
        // Do a config change and restart MaxScale. This would trigger the re-encryption of
        // an already encrypted password.
        test.maxscale->restart();
        test.check_maxctrl("alter maxscale session_trace=" + std::to_string(i + 1));

        res = test.maxscale->ssh_output("grep config_sync_password " + config_path
                                        + " |cut -f2 -d=");

        test.expect(original_pw == res.output,
                    "Iteration %d: Password in persisted config file is different: %s",
                    i, res.output.c_str());
    }

    test.check_maxctrl("alter maxscale session_trace=0");
}

void test_main(TestConnections& test)
{
    create_key(test);
    hash_password(test);
    encrypted_password_in_maxctrl(test);
    mxs5520_no_password_reencryption(test);
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
