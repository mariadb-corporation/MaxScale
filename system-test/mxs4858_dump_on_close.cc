/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;

    auto sMdb = mxs.try_open_rwsplit_connection();

    test.expect(sMdb->is_open(), "Could not open connection to MaxScale RWS.");

    if (sMdb->is_open())
    {
        // In 23.08.(0..3) with
        //
        //   dump_last_statements=on_close
        //   retain_last_statements=10
        //
        // there is a crash at session exit.

        sMdb->query("SELECT 1");
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
