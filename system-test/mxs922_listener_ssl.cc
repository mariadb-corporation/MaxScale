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

    MYSQL* conn = open_conn(test->maxscale->readconn_master_port,
                            test->maxscale->ip4(),
                            test->maxscale->user_name(),
                            test->maxscale->password(),
                            true);
    test->add_result(execute_query(conn, "select @@server_id"), "SSL query to readconnroute failed");
    mysql_close(conn);

    test->maxscale->expect_running_status(true);
    int rval = test->global_result;
    delete test;
    return rval;
}
