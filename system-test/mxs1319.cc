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
 * Check that SQL_MODE='PAD_CHAR_TO_FULL_LENGTH' doesn't break authentication
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Changing SQL_MODE to PAD_CHAR_TO_FULL_LENGTH and restarting MaxScale");
    test.repl->connect();
    test.repl->execute_query_all_nodes("SET GLOBAL SQL_MODE='PAD_CHAR_TO_FULL_LENGTH'");
    test.maxscale->restart_maxscale();

    test.tprintf("Connecting to MaxScale and executing a query");
    test.maxscale->connect_maxscale();
    test.try_query(test.maxscale->conn_rwsplit, "SELECT 1");
    test.maxscale->close_maxscale_connections();

    test.repl->execute_query_all_nodes("SET GLOBAL SQL_MODE=DEFAULT");
    return test.global_result;
}
