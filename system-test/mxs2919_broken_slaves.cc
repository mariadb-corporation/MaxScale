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

/**
 * MXS-2919: Slaves that aren't replicating should not be used for reads when max_slave_replication_lag is
 * used.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work");

    std::string master_id = conn.field("SELECT @@server_id, @@last_insert_id", 0);

    test.repl->connect();

    for (int i = 1; i < test.repl->N; i++)
    {
        test.repl->block_node_from_node(i, 0);
        test.try_query(test.repl->nodes[i], "STOP SLAVE;START SLAVE");
    }

    test.repl->disconnect();
    test.maxscale->wait_for_monitor();

    for (int i = 0; test.ok() && i < 50; i++)
    {
        auto current_id = conn.field("SELECT @@server_id");

        test.expect(current_id == master_id,
                    "The query was not routed to the master: %s%s",
                    current_id.c_str(), conn.error());
    }

    for (int i = 1; i < test.repl->N; i++)
    {
        test.repl->unblock_node_from_node(i, 0);
    }

    return test.global_result;
}
