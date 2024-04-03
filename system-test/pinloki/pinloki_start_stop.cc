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

class StartStopTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        sync_all();
    }

    void run() override
    {
        for (int i = 0; i < 100 && test.ok(); i++)
        {
            test.expect(master.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")"),
                        "INSERT failed: %s", master.error());
            test.expect(maxscale.query("STOP SLAVE"), "STOP SLAVE failed: %s", maxscale.error());
            test.expect(maxscale.query("START SLAVE"), "START SLAVE failed: %s", maxscale.error());
        }

        slave.query("STOP SLAVE;START SLAVE;");
        sync_all();

        // All servers should be at the same GTID
        check_gtid();
    }

    void post() override
    {
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", master.error());
    }

private:
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return StartStopTest(test).result();
}
