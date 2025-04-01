/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include <maxtest/testconnections.hh>
#include <iostream>
#include <sstream>
#include <string>

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::string;
using std::stringstream;

namespace
{

void create_table(TestConnections& test)
{
    MYSQL* pConn = test.maxscale->conn_rwsplit;

    test.try_query(pConn, "DROP TABLE IF EXISTS test.t1");
    test.try_query(pConn, "CREATE TABLE test.t1(id INT)");
}

int i_start = 0;
int n_rows = 20;
int i_end = 0;

void insert_data(TestConnections& test)
{
    MYSQL* pConn = test.maxscale->conn_rwsplit;

    test.try_query(pConn, "BEGIN");

    i_end = i_start + n_rows;

    for (int i = i_start; i < i_end; ++i)
    {
        stringstream ss;
        ss << "INSERT INTO test.t1 VALUES (" << i << ")";
        test.try_query(pConn, "%s", ss.str().c_str());
    }

    test.try_query(pConn, "COMMIT");

    i_start = i_end;
}

void run(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    mxs.wait_for_monitor();

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto normal_status = mxt::ServersInfo::default_repl_states();
    mxs.check_servers_status(normal_status);

    mxs.connect_maxscale();

    test.tprintf("Creating table.");
    create_table(test);

    test.tprintf("Inserting data.");
    insert_data(test);

    test.tprintf("Trying to do manual switchover to server2");
    test.maxctrl("call command mysqlmon switchover MySQL-Monitor server2 server1");

    mxs.wait_for_monitor();
    mxs.check_servers_status({slave, master, slave, slave});

    if (test.ok())
    {
        test.tprintf("Switchover success. Resetting situation using async-switchover.");
        test.maxctrl("call command mariadbmon async-switchover MySQL-Monitor server1");
        // Wait a bit so switch completes, then fetch results.
        mxs.wait_for_monitor(2);
        auto res = test.maxctrl("call command mariadbmon fetch-cmd-result MySQL-Monitor");
        test.expect(res.rc == 0, "fetch-cmd-result failed: %s", res.output.c_str());
        if (test.ok())
        {
            // The output is a json string. Check that it includes "switchover completed successfully".
            auto found = (res.output.find("switchover completed successfully") != string::npos);
            test.expect(found, "Result json did not contain expected message. Result: %s",
                        res.output.c_str());
        }
        mxs.check_servers_status(normal_status);
    }

    if (test.ok())
    {
        test.tprintf("MXS-4605: Monitor should reconnect if command fails due to missing privileges.");
        mxs.stop();
        auto* master_srv = repl.backend(0);
        auto conn = master_srv->open_connection();
        conn->cmd_f("grant slave monitor on *.* to mariadbmon;");
        conn->cmd_f("revoke super, read_only admin on *.* from mariadbmon;");
        repl.sync_slaves();
        // Close connections so monitor does not attempt to kill them.
        conn = nullptr;
        repl.close_connections();
        repl.close_admin_connections();

        mxs.start();

        mxs.check_servers_status(normal_status);
        if (test.ok())
        {
            auto try_switchover = [&](bool expect_success) {
                const string switch_cmd = "call command mysqlmon switchover MySQL-Monitor server2";
                auto res = test.maxctrl(switch_cmd);
                if (expect_success)
                {
                    if (res.rc == 0)
                    {
                        test.tprintf("Switchover succeeded.");
                    }
                    else
                    {
                        test.add_failure("Switchover failed. Error: %s", res.output.c_str());
                    }
                }
                else
                {
                    if (res.rc == 0)
                    {
                        test.add_failure("Switchover succeeded when it should have failed.");
                    }
                    else
                    {
                        test.tprintf("Switchover failed as expected. Error: %s", res.output.c_str());
                        test.expect(res.output.find("Failed to enable read_only on") != string::npos,
                                    "Did not find expected error message.");
                        mxs.check_print_servers_status(normal_status);
                    }
                }
                mxs.wait_for_monitor();
            };

            test.tprintf("Trying switchover, it should fail due to missing privs.");
            try_switchover(false);

            if (test.ok())
            {
                conn = master_srv->open_connection();
                conn->cmd_f("grant super, read_only admin on *.* to mariadbmon;");
                conn = nullptr;

                repl.sync_slaves();
                repl.close_admin_connections();

                test.tprintf("Privileges granted. Switchover should still fail, as monitor connections are "
                             "using the grants of their creation time.");
                try_switchover(false);

                test.tprintf("Switchover should now work.");
                try_switchover(true);

                mxs.check_print_servers_status({slave, master, slave, slave});
            }
        }

        if (!test.ok())
        {
            conn = master_srv->open_connection();
            conn->cmd_f("grant super, read_only admin on *.* to mariadbmon;");
        }
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, run);
}
