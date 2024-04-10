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
 * MXS-3416: Extra OK packet when session command is followed by a causal read
 *
 * https://jira.mariadb.org/browse/MXS-3416
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection failed: %s", conn.error());

    for (int i = 0; i < 1000 && test.ok(); i++)
    {
        test.reset_timeout();
        test.expect(conn.query("SET @a = 1"), "SET should work: %s", conn.error());
        auto res = conn.field("SELECT 2 as two");
        test.expect(res == "2", "Iteration %d: SELECT retured: %s",
                    i, res.empty() ? "an empty string" : res.c_str());
    }

    return test.global_result;
}
