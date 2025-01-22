/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <string>
#include <maxbase/format.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/string.hh>

using std::string;
using mxt::MaxScale;

namespace
{
const string keypath = "/tmp/sshkey.pem";

void copy_ssh_keyfile(TestConnections& test, mxt::MariaDBServer* target1, mxt::MariaDBServer* target2);
void install_tools(TestConnections& test, int ind);
void run_rebuild(TestConnections& test, const string& rebuild_cmd, int target_ind, int master_ind);
void test_special_characters(TestConnections& test, int target_ind, int master_ind);
bool wait_for_cmd_completion(TestConnections& test);

void test_main(TestConnections& test)
{
    const int source_ind = 1;
    const int target_ind = 3;
    auto master_st = mxt::ServerInfo::master_st;
    auto slave_st = mxt::ServerInfo::slave_st;

    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto* source_be = repl.backend(source_ind);
    auto* target_be = repl.backend(target_ind);
    copy_ssh_keyfile(test, source_be, target_be);

    mxs.start();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    // Firewall on the source server may interfere with the transfer, stop it.
    source_be->vm_node().run_cmd_output_sudo("systemctl stop iptables");

    if (test.ok())
    {
        install_tools(test, source_ind);
        install_tools(test, target_ind);
    }

    if (test.ok())
    {
        const int target_rows = 100;
        const int cluster_rows = 300;

        // Stop replication on target, then add a bunch of different data to the target and master.
        auto target_conn = target_be->open_connection();
        target_conn->cmd("stop slave;");
        target_conn->cmd("reset slave all;");

        test.tprintf("Replication on server4 stopped, adding events to it.");
        target_conn->cmd("create or replace database test;");
        target_conn->cmd("create table test.t1 (c1 varchar(100), c2 int);");
        target_conn->cmd("use test;");

        if (test.ok())
        {
            for (int i = 0; i < target_rows; i++)
            {
                target_conn->cmd("insert into t1 values (md5(rand()), rand());");
            }
        }
        mxs.wait_for_monitor(1);
        auto data = mxs.get_servers();
        data.print();

        test.tprintf("Adding events to remaining cluster.");
        auto rwsplit_conn = mxs.open_rwsplit_connection2();
        rwsplit_conn->cmd("create or replace database test;");
        rwsplit_conn->cmd("create table test.t1 (c1 INT, c2 varchar(100));");
        rwsplit_conn->cmd("use test;");

        if (test.ok())
        {
            for (int i = 0; i < cluster_rows; i++)
            {
                rwsplit_conn->cmd("insert into t1 values (rand(), md5(rand()));");
            }
            repl.sync_slaves();
            mxs.wait_for_monitor(1);
            data = mxs.get_servers();
            data.print();
        }

        // Check row counts.
        const string rows_query = "select count(*) from test.t1;";
        auto cluster_rowcount = std::stoi(rwsplit_conn->simple_query(rows_query));
        auto target_rowcount = std::stoi(target_conn->simple_query(rows_query));

        const char rows_mismatch[] = "%s returned %i rows when %i was expected";
        test.expect(cluster_rowcount == cluster_rows, rows_mismatch, "Cluster", cluster_rowcount,
                    cluster_rows);
        test.expect(target_rowcount == target_rows, rows_mismatch, "Target", cluster_rowcount,
                    cluster_rows);

        int master_ind = 0;
        auto server_info = mxs.get_servers();
        server_info.check_servers_status({master_st, slave_st, slave_st, mxt::ServerInfo::RUNNING});
        auto master_gtid = server_info.get(master_ind).gtid;
        auto target_gtid = server_info.get(target_ind).gtid;
        test.expect(master_gtid != target_gtid, "Gtids should have diverged");
        auto master_gtid_parts = mxb::strtok(master_gtid, "-");
        auto target_gtid_parts = mxb::strtok(target_gtid, "-");
        test.expect(master_gtid_parts.size() == 3, "Invalid master gtid");
        test.expect(target_gtid_parts.size() == 3, "Invalid target gtid");

        test.expect(master_gtid_parts[1] != target_gtid_parts[1], "Gtid server_ids should be different");
        if (test.ok())
        {
            string rebuild_cmd = "call command mariadbmon async-rebuild-server MariaDB-Monitor "
                                 "server4 server2";
            run_rebuild(test, rebuild_cmd, target_ind, master_ind);

            if (test.ok())
            {
                test_special_characters(test, target_ind, master_ind);
            }
        }
        rwsplit_conn->cmd("drop database test;");
    }

    source_be->vm_node().run_cmd_output_sudo("systemctl start iptables");
    mxs.vm_node().delete_from_node(keypath);
}

void copy_ssh_keyfile(TestConnections& test, mxt::MariaDBServer* target1, mxt::MariaDBServer* target2)
{
    // Copy ssh keyfile to maxscale VM from server1.
    auto& mxs = *test.maxscale;
    mxs.vm_node().delete_from_node(keypath);
    mxt::Node& key_source = test.repl->backend(0)->vm_node();

    mxs.copy_to_node(key_source.sshkey(), keypath.c_str());
    auto chmod = mxb::string_printf("chmod a+rx %s", keypath.c_str());
    mxs.vm_node().run_cmd(chmod);
    // Read the contents of authorized_keys on server1. Check that the same line exists on server2 & 4.
    // If not, edit the other files.
    const string authorized_keys_path = mxb::string_printf("%s/.ssh/authorized_keys",
                                                           key_source.access_homedir());
    const string read_pubkey_cmd = mxb::string_printf("head -n1 %s", authorized_keys_path.c_str());
    auto pubkey_res = key_source.run_cmd_output(read_pubkey_cmd);

    if (pubkey_res.rc == 0 && !pubkey_res.output.empty())
    {
        test.tprintf("Expecting authorized_keys to contain line '%s'.", pubkey_res.output.c_str());
        string grep_cmd = mxb::string_printf("cat %s | grep \"%s\"", authorized_keys_path.c_str(),
                                             pubkey_res.output.c_str());
        string concat_cmd = mxb::string_printf("echo \"%s\" >> %s", pubkey_res.output.c_str(),
                                               authorized_keys_path.c_str());
        for (auto* be : {target1, target2})
        {
            auto grep_res = be->vm_node().run_cmd_output(grep_cmd);
            if (grep_res.rc != 0)
            {
                test.tprintf("Public key not found on %s, adding it.", be->vm_node().name());
                be->vm_node().run_cmd_output(concat_cmd);
                grep_res = be->vm_node().run_cmd_output(grep_cmd);
                test.expect(grep_res.rc == 0, "Failed to add public key to %s.", be->vm_node().name());
            }
        }
    }
    else
    {
        test.add_failure("Command '%s' failed or gave no results. Error: %s",
                         read_pubkey_cmd.c_str(), pubkey_res.output.c_str());
    }
}

void install_tools(TestConnections& test, int ind)
{
    auto be = test.repl->backend(ind);
    test.tprintf("Installing tools to %s", be->cnf_name().c_str());
    const char install_fmt[] = "yum -y install %s";
    be->vm_node().run_cmd_output_sudof(install_fmt, "pigz");
    be->vm_node().run_cmd_output_sudof(install_fmt, "MariaDB-backup");
}

void run_rebuild(TestConnections& test, const string& rebuild_cmd, int target_ind, int master_ind)
{
    auto& mxs = *test.maxscale;
    auto res = mxs.maxctrl(rebuild_cmd);
    if (res.rc == 0)
    {
        bool op_success = wait_for_cmd_completion(test);
        test.expect(op_success, "Rebuild operation failed.");

        if (op_success)
        {
            // Target should now be a slave and have same gtid as master.
            test.repl->sync_slaves();
            mxs.wait_for_monitor();
            auto server_info = mxs.get_servers();
            server_info.print();
            auto& master = server_info.get(master_ind);
            auto& target = server_info.get(target_ind);
            test.expect(master.is_master(), "%s is not master.", master.name.c_str());
            test.expect(target.is_slave(), "%s is not slave.", target.name.c_str());
            test.expect(master.gtid == target.gtid, "Gtids should be equal");
        }
    }
    else
    {
        test.add_failure("Failed to start rebuild: %s", res.output.c_str());
    }
}

bool wait_for_cmd_completion(TestConnections& test)
{
    bool op_success = false;
    mxb::StopWatch timer;
    while (timer.split() < 30s)
    {
        auto op_status = test.maxscale->maxctrl("call command mariadbmon fetch-cmd-result MariaDB-Monitor");
        if (op_status.rc != 0)
        {
            test.add_failure("Failed to check backup operation status: %s",
                             op_status.output.c_str());
            break;
        }
        else
        {
            auto& out = op_status.output;
            if (out.find("successfully") != string::npos)
            {
                op_success = true;
                break;
            }
            else if (out.find("pending") != string::npos
                     || out.find("running") != string::npos)
            {
                // ok, in progress
            }
            else
            {
                // Either "failed" or something unexpected.
                break;
            }
        }
        sleep(1);
    }
    return op_success;
}

void test_special_characters(TestConnections& test, int target_ind, int master_ind)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    // MXS-5366 Test username/password with special characters. This still does not test
    // a single quote ('), but perhaps that is rare enough to ignore for now. Supporting '
    // would require some extra string processing.
    auto change_monitor_user = [&](const string& user, const string& pw) {
        string cmd = mxb::string_printf("maxctrl alter monitor MariaDB-Monitor "
                                        "user='%s' password='%s'",
                                        user.c_str(), pw.c_str());
        auto rc = mxs.vm_node().run_cmd(cmd);
        test.expect(rc == 0, "Alter monitor command '%s' failed.", cmd.c_str());
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    };

    string tricky_user_str = "\"#¤%&/\\()=?";
    // The backslashes have to be doubled for mariadb client.
    string tricky_user_client = "\"#¤%&/\\\\()=?";
    string tricky_user_pw = "åÄÖ*,.-_";
    auto user = repl.backend(master_ind)->admin_connection()->create_user(
        tricky_user_client, "%", tricky_user_pw);
    user.grant("all privileges on *.*");
    test.tprintf("User '%s' created. Changing monitor user and testing rebuild-server.",
                 tricky_user_str.c_str());
    repl.sync_slaves();
    change_monitor_user(tricky_user_str, tricky_user_pw);

    auto* target_be = repl.backend(target_ind);
    target_be->admin_connection()->cmd("stop slave;");
    mxs.wait_for_monitor();
    string rebuild_cmd = "call command mariadbmon async-rebuild-server MariaDB-Monitor "
                         "server4 server2";
    run_rebuild(test, rebuild_cmd, target_ind, master_ind);

    test.tprintf("Resetting monitor user and password.");
    change_monitor_user("mariadbmon", "mariadbmon");
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}
