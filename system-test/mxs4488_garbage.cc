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
#include <maxtest/tcp_connection.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    for (int limit = 1; limit < 10; limit++)
    {
        tcp::Connection conn;
        conn.connect(test.maxscale->ip(), 4006);

        // The payload will be a partial packet which should trigger the bug.
        std::vector<uint8_t> data{0xff, 0xff, 0xff, 0};

        for (int i = 0; i < limit * 1000; i++)
        {
            conn.write(data.data(), data.size());
        }

        auto rws = test.maxscale->rwsplit();
        test.expect(rws.connect(), "Failed to connect: %s", rws.error());

        for (int i = 0; i < 10; i++)
        {
            test.expect(rws.query("SELECT " + std::to_string(i)), "Failed to query: %s", rws.error());
        }
    }

    return test.global_result;
}
