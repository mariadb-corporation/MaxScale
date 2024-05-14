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
 * MXS-3462: Service-to-service routing doesn't work with the `cluster` parameter.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());
    test.expect(conn.query("SELECT 1"), "SELECT should work: %s", conn.error());
    test.expect(conn.query("SET @a = 1"), "SET should work: %s", conn.error());

    return test.global_result;
}
