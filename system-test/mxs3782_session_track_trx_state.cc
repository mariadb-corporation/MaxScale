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

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("SET GLOBAL session_track_transaction_info=CHARACTERISTICS");


    auto c = test.maxscale->rwsplit();
    c.connect();
    test.tprintf("Disable autocommit and sleep for a while to make sure all servers have executed it");
    c.query("SET autocommit=0");
    sleep(2);

    test.repl->connect();
    auto expected = test.repl->get_server_id_str(0);
    auto id = c.field("SELECT @@server_id");
    test.expect(id == expected, "Expected @@server_id from %s, not from %s", expected.c_str(), id.c_str());

    test.repl->execute_query_all_nodes("SET GLOBAL session_track_transaction_info=OFF");
    return test.global_result;
}
