/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "test_base.hh"

class BasicTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        // Create a table with one row
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        test.expect(master.query("INSERT INTO test.t1 VALUES (1)"), "INSERT failed: %s", master.error());
        test.expect(master.query("FLUSH LOGS"), "FLUSH failed: %s", master.error());
        test.expect(master.query("CREATE TABLE test.t2 (id INT)"), "CREATE failed: %s", master.error());
        test.expect(master.query("INSERT INTO test.t2 VALUES (1)"), "INSERT failed: %s", master.error());
        sync_all();
    }

    void run() override
    {
        // test.t1 should contain one row
        auto result = slave.field("SELECT COUNT(*) FROM test.t1");
        test.expect(result == "1", "`test`.`t1` should have one row.");

        result = slave.field("SELECT COUNT(*) FROM test.t2");
        test.expect(result == "1", "`test`.`t2` should have one row.");

        // All servers should be at the same GTID
        check_gtid();

        // Run the diagnostics function, mainly for code coverage.
        test.check_maxctrl("show services");

        // Some simple sanity checks
        auto rows = maxscale.rows("SHOW MASTER STATUS");
        test.expect(!rows.empty(), "SHOW MASTER STATUS should return a resultset");
        test.expect(!maxscale.query("This should not break anything"), "Bad SQL should fail");
        test.expect(!maxscale.query("CHANGE MASTER 'name' TO MASTER_HOST='localhost'"),
                    "CHANGE MASTER with connection name should fail");

        auto direct = test.repl->backend(2)->admin_connection()->query("SHOW SLAVE STATUS");
        auto c = test.maxscale->open_rwsplit_connection2();

        if (test.ok())
        {
            if (auto via_maxscale = c->query("SHOW SLAVE STATUS"))
            {
                test.expect(direct->next_row(), "Empty direct result");
                test.expect(via_maxscale->next_row(), "Empty maxscale result");

                for (std::string field : {"Master_Log_File", "Read_Master_Log_Pos", "Exec_Master_Log_Pos"})
                {
                    auto expected = direct->get_string(field);
                    auto value = via_maxscale->get_string(field);
                    test.expect(expected == value, "Expected %s to be %s but it was %s",
                                field.c_str(), expected.c_str(), value.c_str());
                }
            }
        }
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
    return BasicTest(test).result();
}
