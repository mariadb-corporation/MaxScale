/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    MYSQL* conn = test.maxscale->open_readconn_master_connection();

    int rc = mysql_query(conn, "CREATE OR REPLACE TABLE test.t1(data varchar(128)) CHARSET utf8mb4");
    test.expect(rc == 0, "CREATE failed: %s", mysql_error(conn));

    std::string query = "INSERT INTO test.t1 VALUES ('🤔')";

    while (query.size() < 0x1fffff)
    {
        query.append(",('🤔')");
    }

    for (int i = 0; i < 10 && test.ok(); i++)
    {
        test.expect(mysql_send_query(conn, query.c_str(), query.size()) == 0,
                    "Batch write failed: %s", mysql_error(conn));
    }

    for (int i = 0; i < 10 && test.ok(); i++)
    {
        test.expect(mysql_read_query_result(conn) == 0,
                    "Batch read failed: %s", mysql_error(conn));
    }

    rc = mysql_query(conn, "DROP TABLE test.t1");
    test.expect(rc == 0, "DROP failed: %s", mysql_error(conn));

    return test.global_result;
}
