/*
 * Copyright (c) 2025 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * The test sets up data encryption and enables it for binlog, redolog and tables. The test then
 * creates an encrypted table, adds data to it, diverges the replica and tests that the replica can
 * be rebuilt from the primary.
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
const char enc_file_path[] = "/tmp/server_enc_key.txt";
const char enc_tbl_name[] = "my_secret_table";
const char inno_enc_tables[] = "innodb_encrypt_tables";
const char inno_enc_log[] = "innodb_encrypt_log";
const char enc_binlog[] = "encrypt_binlog";

void prepare_encryption_key(mxt::MariaDBServer* srv);
void enable_data_encryption(mxt::MariaDBServer* srv);
void disable_data_encryption(mxt::MariaDBServer* srv);
void check_encryption_settings(TestConnections& test, mxt::MariaDBServer* srv, bool expected);
void cleanup_encryption_key(mxt::MariaDBServer* srv);

void test_encrypted_rebuild(TestConnections& test, mxt::MariaDBServer* source, mxt::MariaDBServer* target);
void run_rebuild(TestConnections& test, const string& rebuild_cmd, int target_ind, int master_ind);
bool wait_for_cmd_completion(TestConnections& test);
void check_encr_tbl_row_count(TestConnections& test, mxt::MariaDB* conn, int n_expected);

auto master_st = mxt::ServerInfo::master_st;
auto slave_st = mxt::ServerInfo::slave_st;
auto down = mxt::ServerInfo::DOWN;
auto running = mxt::ServerInfo::RUNNING;

void test_main(TestConnections& test)
{
    const int source_ind = 0;
    const int target_ind = 1;
    const char purge_logs[] = "flush binary logs; purge binary logs before now();";

    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto* source_be = repl.backend(source_ind);
    auto* target_be = repl.backend(target_ind);
    test.tprintf("Shut down servers 3 & 4, need just two servers for this test.");
    repl.stop_node(2);
    repl.stop_node(3);

    backup::copy_ssh_keyfile(test, {source_be, target_be});
    backup::stop_firewall(test, source_ind);
    backup::stop_firewall(test, target_ind);

    test.tprintf("Set up encryption and purge unencrypted binary logs.");
    for (auto* srv : {source_be, target_be})
    {
        srv->stop_database();
        srv->stash_server_settings();
        prepare_encryption_key(srv);
        enable_data_encryption(srv);
        srv->start_database();
        srv->ping_or_open_admin_connection();
        srv->admin_connection()->cmd(purge_logs);
        check_encryption_settings(test, srv, true);
    }

    mxs.start();
    mxs.wait_for_monitor();
    mxs.check_print_servers_status({master_st, slave_st});
    repl.sync_slaves();

    if (test.ok())
    {
        // Tools should have been installed by previous test, but don't assume it. These tools should perhaps
        // be installed once during test run preparation itself
        // TODO
        backup::install_tools(test, source_ind);
        backup::install_tools(test, target_ind);
    }

    if (test.ok())
    {
        test.tprintf("Create an encrypted table.");
        auto rwconn = mxs.open_rwsplit_connection2();
        rwconn->cmd_f("create table %s (i int primary key, msg text) engine=InnoDB "
                      "encrypted=YES encryption_key_id=1;", enc_tbl_name);
        rwconn->cmd_f("insert into %s values (1, 'aaa'), (2, 'bbb');", enc_tbl_name);

        repl.sync_slaves();
        mxs.wait_for_monitor();
        mxs.check_print_servers_status({master_st, slave_st});
        check_encr_tbl_row_count(test, rwconn.get(), 2);

        if (test.ok())
        {
            test.tprintf("Encryption set up, testing rebuild.");
            test_encrypted_rebuild(test, source_be, target_be);
        }

        test.tprintf("Delete the encrypted table.");
        rwconn = mxs.open_rwsplit_connection2();
        rwconn->cmd_f("drop table %s;", enc_tbl_name);
        repl.sync_slaves();
    }

    mxs.wait_for_monitor();
    mxs.check_print_servers_status({master_st, slave_st});

    test.tprintf("Disable encryption.");
    for (auto* srv : {source_be, target_be})
    {
        // Redu/binary log encryption must be disabled first, then server restarted. Only after can the
        // plugin be removed. This also means that if this test crashes, all tests afterward will likely
        // fail.
        srv->stop_database();
        disable_data_encryption(srv);
        srv->start_database();
        srv->ping_or_open_admin_connection();
        srv->admin_connection()->cmd(purge_logs);
        srv->stop_database();
        cleanup_encryption_key(srv);
        srv->restore_server_settings();
        srv->start_database();
        check_encryption_settings(test, srv, false);
    }

    test.tprintf("Restart servers 3 & 4, and restore replication with reset-replication.");
    repl.start_node(2);
    repl.start_node(3);
    mxs.wait_for_monitor();
    mxs.get_servers().print();
    auto res = mxs.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    test.expect(res.rc == 0, "Replication reset failed.");
    mxs.wait_for_monitor();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    backup::start_firewall(test, source_ind);
    backup::start_firewall(test, target_ind);
    backup::delete_ssh_keyfile(test);
}

void test_encrypted_rebuild(TestConnections& test, mxt::MariaDBServer* source, mxt::MariaDBServer* target)
{
    const int orig_rows = 2;
    auto& mxs = *test.maxscale;
    const int target_rows = 50;
    auto target_name = target->cnf_name().c_str();
    test.tprintf("Stop replication on %s, then add more rows to %s on %s.",
                 target_name, enc_tbl_name, target_name);
    auto target_conn = target->open_connection("test");
    target_conn->cmd("stop slave;");
    target_conn->cmd("reset slave all;");

    for (int i = 0; i < target_rows; i++)
    {
        int num = 100 + i;
        target_conn->cmd_f("insert into %s values (%i, 'text%itext');", enc_tbl_name, num, num);
    }
    check_encr_tbl_row_count(test, target_conn.get(), target_rows + orig_rows);

    test.tprintf("Add one row to primary server.");
    auto source_conn = source->open_connection("test");
    source_conn->cmd_f("insert into %s values (123, 'foobar');", enc_tbl_name);
    check_encr_tbl_row_count(test, source_conn.get(), orig_rows + 1);

    mxs.wait_for_monitor(1);
    auto server_info = mxs.get_servers();
    server_info.check_servers_status({master_st, running});

    auto master_gtid = server_info.get(0).gtid;
    auto target_gtid = server_info.get(1).gtid;
    test.expect(master_gtid != target_gtid, "Gtids should have diverged");

    if (test.ok())
    {
        test.tprintf("Rebuilding server2 from server1.");
        string rebuild_cmd = "call command mariadbmon async-rebuild-server MariaDB-Monitor "
                             "server2 server1";
        run_rebuild(test, rebuild_cmd, 1, 0);
        target_conn = target->open_connection("test");
        check_encr_tbl_row_count(test, target_conn.get(), 3);
    }
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

void prepare_encryption_key(mxt::MariaDBServer* srv)
{
    // Shutdown server, add encryption keyfile, add keyfile manager plugin to config.
    const char file_contents[] = "1;c187f18146bdff84350a738090ddc111721109f83edc51b4a276eb7e112bb1af";
    auto res = srv->vm_node().run_cmd_output_sudof("echo \"%s\" > %s", file_contents, enc_file_path);
    srv->add_server_setting("plugin_load_add = file_key_management");
    string key_setting = mxb::string_printf("loose_file_key_management_filename = %s", enc_file_path);
    srv->add_server_setting(key_setting.c_str());
}

void enable_data_encryption(mxt::MariaDBServer* srv)
{
    const char fmt[] = "%s = ON";
    string set_inno_enc_tables = mxb::string_printf(fmt, inno_enc_tables);
    string set_inno_enc_log = mxb::string_printf(fmt, inno_enc_log);
    string set_enc_binlog = mxb::string_printf(fmt, enc_binlog);
    srv->add_server_setting(set_inno_enc_tables.c_str());
    srv->add_server_setting(set_inno_enc_log.c_str());
    srv->add_server_setting(set_enc_binlog.c_str());
}

void disable_data_encryption(mxt::MariaDBServer* srv)
{
    srv->disable_server_setting(inno_enc_tables);
    srv->disable_server_setting(inno_enc_log);
    srv->disable_server_setting(enc_binlog);
}

void check_encryption_settings(TestConnections& test, mxt::MariaDBServer* srv, bool expected)
{
    auto conn = srv->open_connection();
    auto res = conn->query_f("select @@%s, @@%s, @@%s;", inno_enc_tables, inno_enc_log, enc_binlog);
    if (res)
    {
        res->next_row();
        string tables = res->get_string(0);
        int log = res->get_int(1);
        int binlog = res->get_int(2);
        string tables_expected = expected ? "ON" : "OFF";
        int log_expected = expected ? 1 : 0;
        int binlog_expected = expected ? 1 : 0;

        test.tprintf("%s: %s is %s, %s is %i, %s is %i", srv->cnf_name().c_str(),
                     inno_enc_tables, tables.c_str(), inno_enc_log, log, enc_binlog, binlog);
        test.expect(tables == tables_expected, "Wrong value for %s. Got %s, expected %s.",
                    inno_enc_tables, tables.c_str(), tables_expected.c_str());
        const char wrong_value[] = "Wrong value for %s. Got %i, expected %i.";
        test.expect(log == log_expected, wrong_value, inno_enc_log, log, log_expected);
        test.expect(binlog == binlog_expected, wrong_value, enc_binlog, binlog, binlog_expected);
    }
}

void cleanup_encryption_key(mxt::MariaDBServer* srv)
{
    srv->vm_node().delete_from_node(enc_file_path);
}

void check_encr_tbl_row_count(TestConnections& test, mxt::MariaDB* conn, int n_expected)
{
    auto res = conn->query_f("select count(*) from %s;", enc_tbl_name);
    if (res)
    {
        res->next_row();
        int count = res->get_int(0);
        if (count == n_expected)
        {
            test.tprintf("%i rows, as expected.", count);
        }
        else
        {
            test.add_failure("Unexpected number of rows. Expected %i, found %i.",
                             n_expected, count);
        }
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}
