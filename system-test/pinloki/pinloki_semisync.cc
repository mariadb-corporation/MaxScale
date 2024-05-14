/*
 * Copyright (c) 2023 MariaDB plc
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
#include "test_base.hh"
#include <maxbase/string.hh>

class SemiSyncTest : public TestCase
{
public:
    using TestCase::TestCase;

    void setup() override
    {
        test.expect(master.connect(), "Failed to connect to master: %s", master.error());
        test.expect(master.query("SET GLOBAL rpl_semi_sync_master_enabled=ON, "
                                 "rpl_semi_sync_master_timeout=200000"),
                    "Failed to enable semi-sync on master: %s", master.error());

        test.expect(slave.connect(), "Failed to connect to slave: %s", slave.error());
        test.expect(slave.query("SET GLOBAL rpl_semi_sync_slave_enabled=ON"),
                    "Failed to enable semi-sync on slave: %s", slave.error());

        TestCase::setup();
    }

    void run() override
    {
        master.query("SET SESSION max_statement_time=30");
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        test.expect(master.query("INSERT INTO test.t1 VALUES (1)"), "INSERT failed: %s", master.error());
        sync_all();

        // test.t1 should contain one row
        auto result = slave.field("SELECT COUNT(*) FROM test.t1");
        test.expect(result == "1", "`test`.`t1` should have one row.");

        // All servers should be at the same GTID
        check_gtid();

        auto status = master.field("SHOW STATUS LIKE 'Rpl_semi_sync_master_status'", 1);
        test.expect(mxb::tolower(status) == "on",
                    "Rpl_semi_sync_master_status is not ON, it is %s", status.c_str());
    }

    void post() override
    {
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", master.error());

        test.expect(master.query("SET GLOBAL rpl_semi_sync_master_enabled=OFF, "
                                 "rpl_semi_sync_master_timeout=DEFAULT"),
                    "Failed to enable semi-sync on master: %s", master.error());
        test.expect(slave.query("SET GLOBAL rpl_semi_sync_slave_enabled=OFF"),
                    "Failed to enable semi-sync on slave: %s", slave.error());
    }

private:
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return SemiSyncTest(test).result();
}
