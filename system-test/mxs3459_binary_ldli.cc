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

/**
 * MXS-3459: LOAD DATA LOCAL INFILE fails with binary data
 *
 * The query classifier would classify the data sent during the LOAD DATA LOCAL INFILE which caused it to fail
 * if the command byte happened to be one of the prepared statement commands.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    char filename[1024] = "/tmp/mxs3459.XXXXXX";
    int fd = mkstemp(filename);
    std::vector<uint8_t> data(1000, 0x17);

    for (int i = 0; i < 10000; i++)
    {
        write(fd, data.data(), data.size());
        write(fd, "\n", 1);
    }

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connect failed: %s", conn.error());

    test.expect(conn.query("CREATE OR REPLACE TABLE test.t1(id BLOB)"),
                "CREATE failed: %s", conn.error());
    test.expect(conn.query("LOAD DATA LOCAL INFILE '" + std::string(filename) + "' INTO TABLE test.t1"),
                "LOAD DATA LOCAL INFILE failed: %s", conn.error());
    test.expect(conn.query("DROP TABLE test.t1"),
                "DROP failed: %s", conn.error());

    close(fd);
    remove(filename);

    return test.global_result;
}
