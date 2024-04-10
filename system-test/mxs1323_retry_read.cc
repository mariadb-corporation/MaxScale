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
 * Test for MXS-1323.
 * - Check that retried reads work with persistent connections
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();
    std::string master = test.repl->get_server_id_str(0);
    std::string slave = test.repl->get_server_id_str(1);
    test.repl->close_connections();

    auto c = test.maxscale->rwsplit();
    c.connect();
    std::string res = c.field("SELECT @@server_id");
    test.add_result(res != slave, "The slave should respond to the first query: %s", res.c_str());

    c.send_query("SELECT @@server_id, SLEEP(5)");
    sleep(1);
    test.repl->block_node(1);
    test.maxscale->wait_for_monitor();
    res = c.read_query_result_field().value_or("");
    test.add_result(res != master, "The master should respond to the second query: %s", res.c_str());
    test.repl->unblock_node(1);

    return test.global_result;
}
