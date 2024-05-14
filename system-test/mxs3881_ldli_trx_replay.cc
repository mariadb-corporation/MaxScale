/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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

#include <maxtest/testconnections.hh>
#include <fstream>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection failed: %s", conn.error());

    test.expect(conn.query("CREATE OR REPLACE TABLE t1(id INT)"), "Failed to create table: %s", conn.error());

    std::ofstream ofile("data.csv");

    for (int i = 0; i < 1000; i++)
    {
        ofile << i << '\n';
    }

    ofile.close();

    test.expect(conn.query("BEGIN"), "BEGIN failed: %s", conn.error());
    test.expect(conn.query("LOAD DATA LOCAL INFILE 'data.csv' INTO TABLE t1"),
                "LOAD DATA failed: %s", conn.error());
    test.expect(conn.query("COMMIT"), "COMMIT failed: %s", conn.error());
    test.expect(conn.query("DROP TABLE t1"), "DROP failed: %s", conn.error());

    remove("data.csv");

    return test.global_result;
}
