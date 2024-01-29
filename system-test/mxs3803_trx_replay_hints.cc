/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    auto ids = test.repl->get_all_server_ids_str();
    test.repl->close_connections();

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());


    test.tprintf("Routing hints should be ignored in normal transactions");

    test.expect(c.query("START TRANSACTION"),
                "Failed to start transaction: %s", c.error());
    auto res1 = c.field("SELECT @@server_id server2");
    test.expect(res1 == ids[0], "Expected %s, got %s", ids[0].c_str(), res1.c_str());
    test.expect(c.query("COMMIT"), "Failed to commit transaction: %s", c.error());


    test.tprintf("Routing hints should be ignored in read-only transactions");

    test.expect(c.query("START TRANSACTION READ ONLY"),
                "Failed to start read-only transaction: %s", c.error());
    auto res2 = c.field("SELECT @@server_id server1");
    test.expect(res2 == ids[1], "Expected %s, got %s", ids[1].c_str(), res2.c_str());
    test.expect(c.query("COMMIT"), "Failed to commit read-only transaction: %s", c.error());

    return test.global_result;
}
