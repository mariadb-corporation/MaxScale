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

class DDLOnlyTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        // Create a table with one row
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        test.expect(master.query("INSERT INTO test.t1 VALUES (1)"),
                    "INSERT failed: %s", master.error());
        test.expect(master.query("UPDATE test.t1 SET id = 2"),
                    "UPDATE failed: %s", master.error());
        test.expect(master.query("CREATE TABLE test.empty_table(id INT)"),
                    "Second CREATE failed: %s", master.error());
        sync_all();
    }

    void run() override
    {
        auto result = slave.field("SELECT COUNT(*) FROM test.t1");
        test.expect(result == "0", "`test`.`t1` should be empty.");

        result = slave.field("SELECT COUNT(*) FROM test.empty_table");
        test.expect(result == "0", "`test`.`empty_table` should be empty.");
    }

    void post() override
    {
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", master.error());
        test.expect(master.query("DROP TABLE test.empty_table"), "DROP failed: %s", master.error());
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return DDLOnlyTest(test).result();
}
