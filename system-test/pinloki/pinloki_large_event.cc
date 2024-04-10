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

#include <maxtest/testconnections.hh>
#include "test_base.hh"

class LargeEventTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        test.expect(master.query("SET GLOBAL max_allowed_packet=33554432"),
                    "Could not set max_allowed_packet: %s", master.error());
        test.expect(master.query("CREATE TABLE test.t1(d LONGTEXT)"), "CREATE should work: %s",
                    master.error());
        master.disconnect();
        master.connect();
    }

    void run() override
    {
        std::vector<size_t> sizes {
            16777176,
            16777176 + 1,
            16777176 - 1,
            16777176 + 2,
            16777176 - 2,
            16777176 + 10,
            16777176 - 10,
        };

        for (const auto& a : sizes)
        {
            test.expect(master.query("INSERT INTO test.t1 SELECT REPEAT('a', " + std::to_string(a) + ")"),
                        "%ld byte INSERT should work: %s", a, master.error());
        }

        sync_all();
        check_gtid();
    }

    void post() override
    {
        master.query("DROP TABLE test.t1");
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return LargeEventTest(test).result();
}
