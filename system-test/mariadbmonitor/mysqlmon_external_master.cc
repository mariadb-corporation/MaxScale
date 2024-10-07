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
 * Test monitoring and failover with an external master
 */
#include <maxtest/testconnections.hh>
#include <atomic>
#include <thread>

namespace
{

void test_main(TestConnections& test);
}

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto down = mxt::ServerInfo::DOWN;
    auto ext = mxt::ServerInfo::EXT_MASTER;

    // Create a table and a user and start a thread that does writes.
    auto node0 = repl.backend(0)->open_connection();
    node0->cmd("CREATE OR REPLACE TABLE test.t1 (id INT)");
    node0->cmd("DROP USER IF EXISTS 'test'@'%%'");
    node0->cmd("CREATE USER 'test'@'%%' IDENTIFIED BY 'test'");
    node0->cmd("GRANT INSERT, SELECT, UPDATE, DELETE ON *.* TO 'test'@'%%'");
    repl.sync_slaves();

    // A separate writer-thread is not really necessary for this test. Perhaps it's still interesting to have.
    std::atomic<bool> is_running {true};
    auto writer_func = [&mxs, &is_running]() {
        while (is_running)
        {
            MYSQL* conn = open_conn(mxs.rwsplit_port, mxs.ip4(), "test", "test", false);

            for (int i = 0; i < 10; i++)
            {
                if (execute_query_silent(conn, "INSERT INTO test.t1 (SELECT SLEEP(0.5));"))
                {
                    sleep(1);
                    break;
                }
            }
            mysql_close(conn);
        }
    };

    test.tprintf("Start by removing server4 from cluster, then have the current master replicate from it.");
    auto ext_server = repl.backend(3);
    ext_server->admin_connection()->cmd("stop slave;");
    ext_server->admin_connection()->cmd("reset slave all;");
    repl.replicate_from(0, 3);
    mxs.wait_for_monitor(1);
    mxs.check_print_servers_status({master | ext, slave, slave});

    // MXS-5272: Slave of External Server (connecting)
    test.tprintf("Stop server4, should be detected by monitor.");
    ext_server->stop_database();
    mxs.sleep_and_wait_for_monitor(1, 1);
    mxs.check_print_servers_status({master | mxt::ServerInfo::EXT_MASTER_CONNECTING, slave, slave});

    node0->cmd("stop slave io_thread;");
    mxs.wait_for_monitor(1);
    mxs.check_print_servers_status({master | mxt::ServerInfo::EXT_MASTER_IO_STOPPED, slave, slave});

    node0->cmd("stop slave sql_thread;");
    mxs.wait_for_monitor(1);
    mxs.check_print_servers_status({master | mxt::ServerInfo::EXT_MASTER_STOPPED, slave, slave});

    node0->cmd("start slave io_thread;");
    mxs.wait_for_monitor(1);
    mxs.check_print_servers_status({master | mxt::ServerInfo::EXT_MASTER_SQL_STOPPED, slave, slave});

    node0->cmd("start slave sql_thread;");
    mxs.wait_for_monitor(1);
    mxs.check_print_servers_status({master | mxt::ServerInfo::EXT_MASTER_CONNECTING, slave, slave});

    test.tprintf("Start server4 and restart external replication on server1.");
    ext_server->start_database();
    node0->cmd("stop slave;");
    node0->cmd("start slave;");
    mxs.wait_for_monitor();
    mxs.check_print_servers_status({master | ext, slave, slave});

    if (test.ok())
    {
        std::thread thr(writer_func);
        sleep(1);

        test.tprintf("Stop server1, expect server2 to be promoted as the master");
        repl.stop_node(0);
        mxs.wait_for_monitor(3);
        // Because the writer thread is doing writes to server1, and those updates are not yet replicated to
        // server4, the server4->server1 replication will fail due to IO thread error. Thus, server2
        // will also get "Slave of External Server (IO stopped)" once promoted.
        mxs.check_print_servers_status({down, master | mxt::ServerInfo::EXT_MASTER_IO_STOPPED, slave});

        if (test.ok())
        {
            auto restart_slave = [&mxs, &repl](int i) {
                auto conn = repl.backend(i)->admin_connection();
                conn->cmd("stop slave;");
                conn->cmd("start slave;");
                mxs.wait_for_monitor(1);
            };
            sleep(1);
            test.tprintf("Configure master-master replication between server2 and the external server");
            repl.replicate_from(3, 1);
            sleep(1);
            // Restart replication from external server to clear errors.
            restart_slave(1);
            mxs.check_print_servers_status({down, master | ext, slave});

            test.tprintf("Start server1, expect it to rejoin the cluster");
            // Rejoin should redirect the existing external master connection in server1.
            repl.start_node(0);
            mxs.wait_for_monitor(2);
            mxs.check_print_servers_status({slave, master | ext, slave});

            test.tprintf("Stop server2, expect server1 to be promoted as the master. Manually redirect "
                         "external server to server1.");
            repl.stop_node(1);
            mxs.wait_for_monitor(2);
            repl.replicate_from(3, 0);
            sleep(1);
            restart_slave(0);
            mxs.check_servers_status({master | ext, down, slave});

            test.tprintf("Start server2, expect it to rejoin the cluster");
            repl.start_node(1);
            mxs.wait_for_monitor(2);
            mxs.check_servers_status({master | ext, slave, slave});

            auto conn = repl.backend(0)->open_connection();
            conn->cmd("stop slave;");
            conn->cmd("reset slave all;");
        }

        is_running = false;
        thr.join();
    }
}
}
