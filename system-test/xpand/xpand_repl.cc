/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    auto m = test.xpand->get_connection(0);
    m.connect();
    m.query("DROP BINLOG 'binlog_name'");
    test.expect(m.query("CREATE BINLOG 'binlog_name' FORMAT='ROW'"), "CREATE BINLOG: %s", m.error());

    // This makes Xpand replicate from itself by going through MaxScale. We don't actually need separate
    // clusters as Xpand replicating from itself is a logical no-op but still ends up sending traffic which is
    // convenient for us as we're testing how MaxScale behaves.
    std::ostringstream ss;
    ss << "CREATE SLAVE 'slave_name' PARALLEL_LOG = 'binlog_name', SLICES = 4, "
       << "MASTER_HOST = '" << test.maxscale->ip() << "', "
       << "MASTER_USER = '" << test.maxscale->user_name() << "' ,"
       << "MASTER_PASSWORD = '" << test.maxscale->password() << "',"
       << "MASTER_PORT = 4006";

    auto s = test.xpand->get_connection(2);
    s.connect();
    s.query("STOP SLAVE 'slave_name'");
    s.query("DROP SLAVE 'slave_name'");
    test.expect(s.query(ss.str()), "CREATE SLAVE: %s", s.error());
    test.expect(s.query("START SLAVE 'slave_name'"), "START SLAVE: %s", s.error());

    m.query("CREATE TABLE test.t1(id INT)");

    for (int i = 0; i < 10; i++)
    {
        test.expect(m.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")"),
                    "Query failed: %s", m.error());
    }

    // Give it a few seconds to process the stuff
    sleep(3);

    auto status = s.field("SHOW SLAVE STATUS 'slave_name'", 1);
    test.expect(status == "Running", "Expected status to be 'Running' but it was '%s'", status.c_str());

    m.query("DROP TABLE test.t1");
    s.query("STOP SLAVE 'slave_name'");
    s.query("DROP SLAVE 'slave_name'");
    m.query("DROP BINLOG 'binlog_name'");
}

int main(int argc, char* argv[])
{
    return TestConnections{}.run_test(argc, argv, test_main);
}
