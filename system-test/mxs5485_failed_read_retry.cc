/*
 * Copyright (c) 2025 MariaDB plc
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

#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("SELECT 1"));

    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("SET @a=1"));
    MXT_EXPECT(c.query("SELECT 1"));
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
