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
 * MXS-1849: Table family sharding router test
 *
 * https://jira.mariadb.org/browse/MXS-1849
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.reset_timeout();
    test.repl->execute_query_all_nodes("STOP SLAVE");
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS shard_db");
    test.repl->execute_query_all_nodes("CREATE DATABASE shard_db");
    MYSQL* conn;
    for (int i = 0; i < test.repl->N; i++)
    {
        conn = open_conn_db(test.repl->port[i],
                            test.repl->ip4(i),
                            "shard_db",
                            test.repl->user_name(),
                            test.repl->password(),
                            test.maxscale_ssl);
        execute_query(conn, "CREATE TABLE table%d (x1 int, fl int)", i);
        mysql_close(conn);
    }

    conn = test.maxscale->open_rwsplit_connection();
    // Check that queries are routed to the right shards
    for (int i = 0; i < test.repl->N; i++)
    {
        test.add_result(execute_query(conn, "SELECT * FROM shard_db.table%d", i), "Query should succeed.");
    }

    mysql_close(conn);
    // Cleanup
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS shard_db");
    test.repl->execute_query_all_nodes("START SLAVE");
    sleep(1);
    return test.global_result;
}
