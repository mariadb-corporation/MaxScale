/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{
void copy_files(TestConnections& test, mxt::MariaDBServer* srv, const string& dir)
{
    auto& node = srv->vm_node();
    string dest_dir = mxb::string_printf("/tmp/%s", dir.c_str());
    auto res = node.run_cmd_output(mxb::string_printf("mkdir -p %s", dest_dir.c_str()));
    if (res.rc == 0)
    {
        // Copy client key & cert to node. Ca cert is already on the node.
        std::string client_cert_src = mxb::string_printf("%s/ssl-cert/client.crt", mxt::SOURCE_DIR);
        std::string client_key_src = mxb::string_printf("%s/ssl-cert/client.key", mxt::SOURCE_DIR);
        node.copy_to_node(client_cert_src, dest_dir);
        node.copy_to_node(client_key_src, dest_dir);

        string src_dir = mxb::string_printf("%s/ssl-cert", node.access_homedir());
        string copy_cmd = mxb::string_printf("cp --remove-destination %s/ca.crt %s",
                                             src_dir.c_str(), dest_dir.c_str());
        res = node.run_cmd_output(copy_cmd);
        test.expect(res.rc == 0, "Certificate copy failed: %s", res.output.c_str());
    }
    else
    {
        test.add_failure("mkdir fail: %s", res.output.c_str());
    }
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        // Create a replication user which requires the slave to connect with certificate.
        int master_ind = 0;
        mxt::MariaDBUserDef cert_user;
        const char username[] = "ssl_replicator";
        cert_user.name = username;
        cert_user.password = username;
        cert_user.host = "%";
        cert_user.grants = {"replication slave on *.*"};
        repl.backend(master_ind)->create_user(cert_user, mxt::MariaDBServer::SslMode::OFF, true);
        auto master_conn = repl.backend(master_ind)->open_connection();
        master_conn->cmd_f("alter user %s@'%%' REQUIRE X509;", username);

        // Copy the client certs to slightly different locations to demonstrate that the server specific
        // settings work.
        test.tprintf("Replication user with certificate requirement created. Copying certificates...");
        std::vector<string> dirs = {"certs_server1", "certs_common", "certs_server3", "certs_common"};
        for (int i = 0; i < repl.N; i++)
        {
            copy_files(test, repl.backend(i), dirs[i]);
        }

        if (test.ok())
        {
            test.tprintf("Running switchover");
            mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor");
            mxs.wait_for_monitor(1);
            auto state_after_switch = {slave, master, slave, slave};
            mxs.check_print_servers_status(state_after_switch);
            master_ind = 1;
            test.expect(repl.sync_slaves(master_ind, 1), "Servers did not sync after switch.");

            auto is_using_correct_user = [&test, username](mxt::MariaDBServer* srv) {
                const char* srvname = srv->cnf_name().c_str();
                auto conn = srv->open_connection();
                auto res = conn->query("show all slaves status;");
                if (res && res->next_row())
                {
                    auto found_user = res->get_string("Master_User");
                    auto found_cert = res->get_string("Master_SSL_Cert");
                    test.tprintf("Replication to %s: username: '%s' certificate: '%s'", srvname,
                                 found_user.c_str(), found_cert.c_str());
                    test.expect(found_user == username,
                                "Replication to %s is using wrong username. Found '%s'.",
                                srvname, found_user.c_str());

                    test.expect(!found_cert.empty(), "Replication to %s is not using a certificate.",
                                srvname);
                }
            };
            is_using_correct_user(repl.backend(0));
            is_using_correct_user(repl.backend(2));
            is_using_correct_user(repl.backend(3));

            auto conn = mxs.open_rwsplit_connection2_nodb();
            conn->cmd("flush tables;");
            mxs.sleep_and_wait_for_monitor(1, 1);
            mxs.check_print_servers_status(state_after_switch);
            test.expect(repl.sync_slaves(master_ind, 1), "Servers did not sync after flush.");

            mxs.alter_monitor("MariaDB-Monitor", "replication_user", "repl");
            mxs.alter_monitor("MariaDB-Monitor", "replication_password", "repl");

            if (test.ok())
            {
                test.tprintf("Switchover back to server1");
                mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor server1");
                master_ind = 0;
                mxs.wait_for_monitor(2);
                test.expect(repl.sync_slaves(master_ind, 1), "Servers did not sync after switch.");

                for (int i = 0; i < repl.N; i++)
                {
                    test.tprintf("Reset replication on server%i", i + 1);
                    conn = repl.backend(i)->open_connection();
                    conn->cmd("stop slave;");
                    // Server saves SSL-settings to a file and will use them later automatically. Need to
                    // clear them here manually so the saved settings also reset.
                    conn->cmd("change master to master_host='127.0.0.1', master_ssl=0, master_ssl_cert='', "
                              "master_ssl_key='', master_ssl_ca='';");
                    conn->cmd("reset slave all;");

                    if (i != master_ind)
                    {
                        repl.replicate_from(i, master_ind);
                    }
                }

                mxs.wait_for_monitor(1);
                mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
            }
            else
            {
                // Replication may be broken, reset everything.
                test.tprintf("Test failed, fix replication.");
                test.repl->fix_replication();
            }
        }

        mxs.open_rwsplit_connection2_nodb()->cmd_f("drop user %s;", username);

        auto rm_certs = [&test](mxt::MariaDBServer* srv, const string& dir) {
            auto res = srv->vm_node().run_cmd_output(mxb::string_printf("rm -rf /tmp/%s", dir.c_str()));
            test.expect(res.rc == 0, "rm fail: %s", res.output.c_str());
        };

        for (int i = 0; i < repl.N; i++)
        {
            rm_certs(repl.backend(i), dirs[i]);
        }
    }
}
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
