/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * @file mxs922_server.cpp MXS-922: Server creation test
 *
 */

#include <maxtest/config_operations.hh>
#include <maxtest/testconnections.hh>

int check_server_id(TestConnections* test, int idx)
{
    test->maxscale->close_maxscale_connections();
    test->maxscale->connect_maxscale();

    int a = test->repl->get_server_id(idx);
    int b = -1;
    char str[1024];

    if (find_field(test->maxscale->conn_rwsplit, "SELECT @@server_id", "@@server_id", str) == 0)
    {
        b = atoi(str);
    }

    return a - b;
}

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    mxt::Config config(test);

    config.create_all_listeners();
    config.create_monitor("mysql-monitor", "mysqlmon", 500);

    test->tprintf("Testing server creation and destruction");

    config.create_server(1);
    config.create_server(1, mxt::Config::Expect::FAIL);
    config.check_server_count(1);
    config.destroy_server(1);
    config.destroy_server(1, mxt::Config::Expect::FAIL);
    config.check_server_count(0);
    test->maxscale->expect_running_status(true);

    test->tprintf("Testing adding of server to service");

    config.create_server(1);
    config.add_server(1);
    config.check_server_count(1);
    sleep(1);
    test->check_maxscale_alive();
    config.remove_server(1);
    config.destroy_server(1);
    config.check_server_count(0);

    test->tprintf("Testing altering of server");

    config.create_server(1);
    config.add_server(1);
    config.alter_server(1, "address", test->repl->ip_private(1));
    sleep(1);
    test->check_maxscale_alive();
    config.alter_server(1, "address", "127.0.0.1");
    config.alter_server(1, "port", 12345);
    test->maxscale->connect_maxscale();
    test->add_result(execute_query_silent(test->maxscale->conn_rwsplit, "SELECT 1") == 0,
                     "Query with bad address should fail");

    config.remove_server(1);
    config.destroy_server(1);

    config.reset();
    sleep(1);
    test->check_maxscale_alive();
    int rval = test->global_result;
    delete test;
    return rval;
}
