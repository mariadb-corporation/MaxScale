/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * @file mxs922_double_listener.cpp MXS-922: Double creation of listeners
 *
 * Check that MaxScale doesn't crash when the same listeners are created twice.
 */

#include <maxtest/config_operations.hh>

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    mxt::Config config(test);

    config.create_all_listeners();
    config.create_all_listeners(mxt::Config::Expect::FAIL);
    test->maxscale->expect_running_status(true);

    config.create_monitor("mysql-monitor", "mysqlmon", 500);
    config.reset();

    test->maxscale->wait_for_monitor();

    test->check_maxscale_alive();
    int rval = test->global_result;
    delete test;
    return rval;
}
