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

/**
 * @file mxs922_listener_ssl.cpp MXS-922: Dynamic SSL test
 */

#include <maxtest/config_operations.hh>
#include <maxtest/testconnections.hh>

using mxt::Config;

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    Config config(test);

    config.create_listener(Config::SERVICE_RWSPLIT);
    config.create_monitor("mysql-monitor", "mysqlmon", 500);
    config.reset();
    test->maxscale->wait_for_monitor();

    test->maxscale->connect_maxscale();
    test->try_query(test->maxscale->conn_rwsplit, "select @@server_id");
    config.create_ssl_listener(Config::SERVICE_RCONN_SLAVE);

    auto& mxs = *test->maxscale;
    auto conn = mxs.try_open_connection(mxt::MaxScale::SslMode::ON, mxs.readconn_slave_port,
                                        mxs.user_name(), mxs.password(), "test");
    test->expect(conn->is_open(), "Connection failed.");
    auto res = conn->simple_query("select @@server_id");
    test->expect(!res.empty(), "Query failed.");

    test->maxscale->expect_running_status(true);
    int rval = test->global_result;
    delete test;
    return rval;
}
