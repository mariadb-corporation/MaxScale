/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Check that using no password returns correct error message
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto mxs_ip = test.maxscale->ip4();
    MYSQL* mysql = open_conn(test.maxscale->rwsplit_port, mxs_ip, "testuser", "", false);
    test.add_result(mysql_errno(mysql) == 0, "Connecting to MaxScale should fail");
    test.add_result(strstr(mysql_error(mysql), "using password: NO") == NULL,
                    "Missing (using password: NO) error message, got this instead: %s",
                    mysql_error(mysql));
    test.tprintf("MySQL error: %s", mysql_error(mysql));
    mysql_close(mysql);

    mysql = open_conn(test.maxscale->rwsplit_port, mxs_ip, "testuser", "testpassword", false);
    test.add_result(mysql_errno(mysql) == 0, "Connecting to MaxScale should fail");
    test.add_result(strstr(mysql_error(mysql), "using password: YES") == NULL,
                    "Missing (using password: YES) error message, got this instead: %s",
                    mysql_error(mysql));
    test.tprintf("MySQL error: %s", mysql_error(mysql));
    mysql_close(mysql);

    return test.global_result;
}
