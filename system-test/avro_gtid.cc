/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>
#include <cdc_connector.h>

using std::string;

void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto& mxs = *test.maxscale;
    const auto N = repl.N;
    const int avro_delay = 8;

    // Stop replication and delete binlogs.
    repl.ping_or_open_admin_connections();
    for (int i = 1; i < N; i++)
    {
        auto conn = repl.backend(i)->admin_connection();
        conn->cmd("stop slave;");
        conn->cmd("reset slave all;");
    }
    for (int i = 0; i < N; i++)
    {
        repl.backend(i)->admin_connection()->cmd("reset master;");
    }

    const char flush[] = "flush tables;";
    const char tbl[] = "test.t1";
    auto conn = repl.backend(0)->open_connection();

    conn->cmd("set global gtid_slave_pos='0-1-0';");
    conn->cmd(flush);
    conn->cmd("create or replace database test;");
    conn->cmd_f("create table %s (c1 int);", tbl);

    auto print_gtids = [&test, &conn]() {
            const string gtid_query = "select @@gtid_current_pos,@@gtid_binlog_pos,@@gtid_slave_pos;";
            auto res = conn->query(gtid_query);
            test.expect(res && res->next_row(), "Failed to query gtids.");
            if (test.ok())
            {
                test.tprintf(
                    "Server @@gtid_current_pos: '%s', @@gtid_binlog_pos: '%s', @@gtid_slave_pos: '%s'",
                    res->get_string(0).c_str(),
                    res->get_string(1).c_str(),
                    res->get_string(2).c_str());
            }
        };

    int i = 0;
    const char insert_fmt[] = "insert into %s values (%i);";
    for (; i < 5; i++)
    {
        conn->cmd_f(insert_fmt, tbl, i);
    }
    print_gtids();

    // Forces server to stop current binlog file and start another.
    conn->cmd("flush logs;");
    conn->cmd("set @@session.gtid_domain_id=1234;");
    for (; i < 10; i++)
    {
        conn->cmd_f(insert_fmt, tbl, i);
    }
    print_gtids();

    if (test.ok())
    {
        mxs.start();
        mxs.wait_for_monitor();
        mxs.get_servers().print();
        test.tprintf("MaxScale started, waiting for Avro to process...");
        sleep(avro_delay);
        mxs.expect_running_status(true);
        mxs.stop();
        // MaxScale should have now processed all binlogs and saved its spot.

        const string show_binlogs = "show binary logs;";
        auto res = conn->query(show_binlogs);
        auto files_before = res ? res->get_row_count() : -1;
        test.expect(files_before > 1, "Not enough binary log files. Found %li, expected at least 2.",
                    files_before);
        if (test.ok())
        {
            // Need the name of the last binlog file.
            for (int j = 0; j < files_before; j++)
            {
                res->next_row();
            }
            string last_logfile = res->get_string(0);
            test.tprintf("Deleting binlog files up to '%s'.", last_logfile.c_str());
            conn->cmd_f("purge binary logs to '%s';", last_logfile.c_str());
            // Check that only one binlog file remains.
            res = conn->query(show_binlogs);
            auto files_remaining = res ? res->get_row_count() : -1;
            test.expect(files_remaining == 1, "Binlog purge failed. Expected one file, found %li.",
                        files_remaining);
            print_gtids();
        }

        // Start MaxScale. It should not complain about missing binlog files.
        mxs.start();
        sleep(avro_delay);
        // If the following log message is changed in MaxScale, this test becomes useless.
        test.log_excludes("Failed to read replicated event");
    }

    conn->cmd("drop database test;");
    auto res = mxs.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor");
    test.expect(res.rc == 0, "reset-replication failed: %s", res.output.c_str());
    sleep(2);
    conn->cmd(flush);
    repl.sync_slaves();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}
