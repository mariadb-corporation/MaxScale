/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-21
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
#include "mariadbmon_utils.hh"

using std::string;
using mxt::MaxScale;

namespace
{
void prepare_to_test_rebuild(TestConnections& test, int target_ind, int master_ind);
void run_rebuild(TestConnections& test, const string& rebuild_cmd, int target_ind, int master_ind);
void test_special_characters(TestConnections& test, int target_ind, int master_ind);
bool wait_for_cmd_completion(TestConnections& test);
void test_rebuild_autoselect(TestConnections& test, int target_ind, int master_ind);
void test_create_restore_backup(TestConnections& test);
void check_value(TestConnections& test, mxt::MariaDB* conn, int expected);
bool command_ok(TestConnections& test, mxt::CmdResult& res, bool cmd_success, const string& backup_cmd);

auto master_st = mxt::ServerInfo::master_st;
auto slave_st = mxt::ServerInfo::slave_st;
auto down = mxt::ServerInfo::DOWN;
auto running = mxt::ServerInfo::RUNNING;

void test_main(TestConnections& test)
{
    const int source_ind = 1;
    const int target_ind = 3;

    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto* source_be = repl.backend(source_ind);
    auto* target_be = repl.backend(target_ind);
    backup::copy_ssh_keyfile(test, {source_be, target_be});

    mxs.start();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    // Firewall may interfere with the transfer, stop it on all servers.
    for (int i = 0; i < repl.N; i++)
    {
        backup::stop_firewall(test, i);
    }

    if (test.ok())
    {
        backup::install_tools(test, source_ind);
        backup::install_tools(test, target_ind);
    }

    if (test.ok())
    {
        int master_ind = 0;
        prepare_to_test_rebuild(test, target_ind, master_ind);

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

        if (test.ok())
        {
            test_rebuild_autoselect(test, target_ind, master_ind);
        }
        auto rwsplit_conn = mxs.open_rwsplit_connection2();
        rwsplit_conn->cmd("drop database test;");
    }

    if (test.ok())
    {
        test_create_restore_backup(test);
    }

    for (int i = 0; i < repl.N; i++)
    {
        backup::start_firewall(test, i);
    }
    backup::delete_ssh_keyfile(test);
}

void prepare_to_test_rebuild(TestConnections& test, int target_ind, int master_ind)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    const int target_rows = 100;
    const int cluster_rows = 300;

    // Stop replication on target, then add a bunch of different data to the target and master.
    auto target_conn = repl.backend(target_ind)->open_connection();
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
    auto change_monitor_user = [&](const string& user, const string& pw){
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

void test_rebuild_autoselect(TestConnections& test, int target_ind, int master_ind)
{
    test.tprintf("Stop server3 and server4. Rebuild server4 without defining source server. "
                 "server2 should be used as source.");
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    int stopped_server_ind = 2;
    repl.backend(stopped_server_ind)->stop_database();
    auto conn = repl.backend(target_ind)->open_connection();
    conn->cmd("stop slave;");
    conn->cmd("flush tables;");
    mxs.wait_for_monitor();
    auto server_info = mxs.get_servers();
    server_info.print();
    auto master_gtid = server_info.get(master_ind).gtid;
    auto target_gtid = server_info.get(target_ind).gtid;
    test.expect(master_gtid != target_gtid, "Gtids should have diverged");
    repl.backend(target_ind)->stop_database();
    mxs.wait_for_monitor();

    string rebuild_cmd = "call command mariadbmon async-rebuild-server MariaDB-Monitor server4";
    run_rebuild(test, rebuild_cmd, target_ind, master_ind);
    mxs.check_print_servers_status({master_st, slave_st, down, slave_st});
    repl.backend(stopped_server_ind)->start_database();
    repl.backend(target_ind)->start_database();
    mxs.wait_for_monitor();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}

void test_create_restore_backup(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    // Normal rebuild works. Test backup creation and use. Backup storage has been configured for
    // server4. To speed up backup creation, minimize binary logs on all servers.
    test.tprintf("Prepare to test create-backup and restore-from-backup. First, truncate binlogs.");
    repl.ping_or_open_admin_connections();
    for (int i = 0; i < repl.N; i++)
    {
        auto conn = repl.backend(i)->admin_connection();
        conn->cmd("reset master;");
    }
    // Reset replication to sync gtids.
    const string reset_repl = "call command mariadbmon reset-replication MariaDB-Monitor server1";
    mxs.maxctrl(reset_repl);
    mxs.wait_for_monitor(2);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    auto rwsplit_conn = mxs.open_rwsplit_connection2_nodb();
    rwsplit_conn->cmd("create or replace database test;");
    rwsplit_conn->cmd("create table test.t1 (id int);");
    rwsplit_conn->cmd("insert into test.t1 values (0);");
    mxs.wait_for_monitor();
    repl.sync_slaves();

    test.tprintf("Binlogs minimized, test database prepared.");
    int bu_storage_ind = 3;
    repl.stop_node(bu_storage_ind);

    test.tprintf("Preparing backup directory.");
    // At this point, clear the backup folder. It may contain old backups from a previous failed
    // test run.
    const char bu_dir[] = "/tmp/backups";
    auto& bu_vm = repl.backend(bu_storage_ind)->vm_node();
    auto clear_backups = [&bu_vm, &bu_dir]() {
        bu_vm.run_cmd_output_sudof("rm -rf %s", bu_dir);
    };
    clear_backups();

    // Recreate backup directory and give ownership.
    bu_vm.run_cmd_output_sudof("mkdir %s", bu_dir);
    auto* ssh_user = mxs.vm_node().access_user();
    bu_vm.run_cmd_output_sudof("sudo chown %s:%s %s", ssh_user, ssh_user, bu_dir);

    const int bu_target_ind = 0;
    backup::install_tools(test, bu_target_ind);     // Backup tools may be missing from server1.

    if (test.ok())
    {
        const char update_cmd[] = "update test.t1 set id=%i;";
        int values[4] = {1234, 5678, 1000001, 3141596};
        int new_val = values[0];
        rwsplit_conn->cmd_f(update_cmd, new_val);
        check_value(test, rwsplit_conn.get(), new_val);

        test.tprintf("Creating backups.");
        const char create_backup_fmt[] = "call command mariadbmon async-create-backup "
                                         "MariaDB-Monitor server1 bu%i";
        for (int i = 1; i <= 3; i++)
        {
            string backup_cmd = mxb::string_printf(create_backup_fmt, i);
            auto res = mxs.maxctrl(backup_cmd);
            bool bu_ok = wait_for_cmd_completion(test);

            if (command_ok(test, res, bu_ok, backup_cmd))
            {
                test.tprintf("Backup %i created.", i);
                // Make a small update so that all backups are different.
                new_val = values[i];
                rwsplit_conn->cmd_f(update_cmd, new_val);
                check_value(test, rwsplit_conn.get(), new_val);
            }
        }

        mxs.wait_for_monitor();
        mxs.get_servers().print();
        test.tprintf("Stopping replication, then stopping all servers.");

        for (int i = 1; i < 3; i++)
        {
            auto slave = repl.backend(i)->admin_connection();
            slave->cmd("stop slave;");
            slave->cmd("reset slave all;");
        }
        mxs.wait_for_monitor();

        // Backup storage should now have three backups. Restore from the
        // second one. Master servers cannot be rebuilt so just shut it down
        // before restoration. Shut down other servers as well to prevent
        // master promotion.
        repl.stop_node(0);
        repl.stop_node(1);
        repl.stop_node(2);

        test.tprintf("Restoring from backup 2.");
        string restore_cmd = "call command mariadbmon async-restore-from-backup "
                             "MariaDB-Monitor server1 bu2";
        auto res = mxs.maxctrl(restore_cmd);
        bool restore_ok = wait_for_cmd_completion(test);
        mxs.wait_for_monitor();

        if (command_ok(test, res, restore_ok, restore_cmd))
        {
            test.tprintf("Restore success, checking contents of server1.");
            auto conn = repl.backend(bu_target_ind)->open_connection();
            check_value(test, conn.get(), values[1]);
            mxs.check_print_servers_status({master_st, down, down});

            // Finally, make server2 master and have all replicate from it.
            // Then, restore server1 from bu1 and check that it rejoins the
            // cluster.
            repl.start_node(1);
            repl.start_node(2);
            mxs.sleep_and_wait_for_monitor(1, 1);
            mxs.check_print_servers_status({master_st, running, running});

            repl.replicate_from(0, 1);
            repl.replicate_from(2, 1);
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({slave_st, master_st, slave_st});
            repl.sync_slaves(1, 5);

            test.tprintf("Rebuild server1 with master (server2) running, "
                         "check that server1 rejoins cluster.");
            repl.stop_node(bu_target_ind);
            restore_cmd = "call command mariadbmon async-restore-from-backup "
                          "MariaDB-Monitor server1 bu1";
            res = mxs.maxctrl(restore_cmd);
            restore_ok = wait_for_cmd_completion(test);
            mxs.wait_for_monitor();

            if (command_ok(test, res, restore_ok, restore_cmd))
            {
                test.tprintf("Restore success.");
                mxs.check_print_servers_status({slave_st, master_st, slave_st});
                test.expect(repl.sync_slaves(1, 5), "server1 did not sync with master");
            }
            repl.start_node(bu_target_ind);
        }

        repl.start_node(0);
        repl.start_node(1);
        repl.start_node(2);
        mxs.wait_for_monitor();
    }
    clear_backups();
    repl.start_node(bu_storage_ind);

    repl.ping_or_open_admin_connections();
    for (int i = 0; i < repl.N; i++)
    {
        repl.backend(i)->admin_connection()->cmd("drop database if exists test;");
    }
    mxs.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    mxs.wait_for_monitor(2);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}

void check_value(TestConnections& test, mxt::MariaDB* conn, int expected)
{
    std::this_thread::sleep_for(100ms);     // Sleep a little to let update propagate.
    string query = "select * from test.t1;";
    auto res = conn->query(query);
    if (res && res->next_row() && res->get_col_count() == 1)
    {
        int found = res->get_int(0);
        test.tprintf("Found value %i.", found);
        test.expect(found == expected, "Found wrong value in test.t1. Got %i, expected %i",
                    found, expected);
    }
    else
    {
        test.add_failure("Query '%s' failed or returned invalid data.", query.c_str());
    }
}

bool command_ok(TestConnections& test, mxt::CmdResult& start_res, bool cmd_success, const string& cmd_str)
{
    bool rval = true;
    if (start_res.rc != 0)
    {
        test.add_failure("Command '%s' startup failed. Error %i: %s", cmd_str.c_str(),
                         start_res.rc, start_res.output.c_str());
        rval = false;
    }
    else if (!cmd_success)
    {
        test.add_failure("Command '%s' failed. Check MaxScale log for more info.", cmd_str.c_str());
        rval = false;
    }
    return rval;
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}
