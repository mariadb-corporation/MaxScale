/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <string>
#include <maxbase/format.hh>
#include <maxbase/stopwatch.hh>

using std::string;
using mxt::MaxScale;

void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    const int source_ind = 1;
    const int target_ind = 3;
    auto master_st = mxt::ServerInfo::master_st;
    auto slave_st = mxt::ServerInfo::slave_st;

    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    // Copy ssh keyfile to maxscale VM.
    const string keypath = "/tmp/sshkey.pem";
    mxs.vm_node().delete_from_node(keypath);
    mxs.copy_to_node(repl.backend(0)->vm_node().sshkey(), keypath.c_str());
    auto chmod = mxb::string_printf("chmod a+rx %s", keypath.c_str());
    mxs.vm_node().run_cmd(chmod);
    auto* target_be = repl.backend(target_ind);

    mxs.start();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        // Need to install some packages.
        auto install_tools = [&repl](int ind) {
            auto be = repl.backend(ind);
            const char install_fmt[] = "yum -y install %s";
            be->vm_node().run_cmd_output_sudof(install_fmt, "pigz");
            be->vm_node().run_cmd_output_sudof(install_fmt, "MariaDB-backup");
        };
        install_tools(source_ind);
        install_tools(target_ind);

        // Firewall on the source server may interfere with the transfer, stop it.
        repl.backend(source_ind)->vm_node().run_cmd_output_sudo("systemctl stop iptables");
    }

    if (test.ok())
    {
        const int target_rows = 100;
        const int cluster_rows = 300;

        // Stop replication on target, then add a bunch of different data to the target and master.
        auto target_conn = target_be->open_connection();
        target_conn->cmd("stop slave;");
        target_conn->cmd("reset slave all;");

        if (test.ok())
        {
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
        }

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
            auto data = mxs.get_servers();
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

        auto server_info = mxs.get_servers();
        server_info.check_servers_status({master_st, slave_st, slave_st, mxt::ServerInfo::RUNNING});
        auto master_gtid = server_info.get(0).gtid;
        auto target_gtid = server_info.get(target_ind).gtid;
        test.expect(master_gtid != target_gtid, "Gtids should have diverged");
        auto master_gtid_parts = mxb::strtok(master_gtid, "-");
        auto target_gtid_parts = mxb::strtok(target_gtid, "-");
        test.expect(master_gtid_parts.size() == 3, "Invalid master gtid");
        test.expect(target_gtid_parts.size() == 3, "Invalid target gtid");

        if (test.ok())
        {
            test.expect(master_gtid_parts[1] != target_gtid_parts[1], "Gtid server_ids should be different");
            if (test.ok())
            {
                auto res = mxs.maxctrl("call command mariadbmon async-rebuild-server MariaDB-Monitor "
                                       "server4 server2");
                if (res.rc == 0)
                {
                    // The op is async, so wait.
                    bool op_success = false;
                    mxb::StopWatch timer;
                    while (timer.split() < 30s)
                    {
                        auto op_status = mxs.maxctrl("call command mariadbmon fetch-cmd-result "
                                                     "MariaDB-Monitor");
                        if (op_status.rc != 0)
                        {
                            test.add_failure("Failed to check rebuild status: %s", op_status.output.c_str());
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

                    test.expect(op_success, "Rebuild operation failed.");

                    if (test.ok())
                    {
                        // server4 should now be a slave and have same gtid as master.
                        repl.sync_slaves();
                        server_info = mxs.get_servers();
                        server_info.print();
                        mxs.wait_for_monitor();
                        server_info.check_servers_status(mxt::ServersInfo::default_repl_states());
                        master_gtid = server_info.get(0).gtid;
                        target_gtid = server_info.get(target_ind).gtid;
                        test.expect(master_gtid == target_gtid, "Gtids should be equal");
                    }
                }
                else
                {
                    test.add_failure("Failed to start rebuild: %s", res.output.c_str());
                }
            }
        }
        rwsplit_conn->cmd("drop database test;");
    }

    repl.backend(source_ind)->vm_node().run_cmd_output_sudo("systemctl start iptables");
    mxs.vm_node().delete_from_node(keypath);
}
