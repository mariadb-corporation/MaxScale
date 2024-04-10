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
 * MXS-1543: https://jira.mariadb.org/browse/MXS-1543
 *
 * Avrorouter doesn't detect MIXED or STATEMENT format replication
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.repl->connect();
    execute_query(test.repl->nodes[0], "RESET MASTER");
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE t1 (data VARCHAR(30))");
    execute_query(test.repl->nodes[0], "INSERT INTO t1 VALUES ('ROW')");
    execute_query(test.repl->nodes[0], "SET binlog_format=STATEMENT");
    execute_query(test.repl->nodes[0], "FLUSH LOGS");
    execute_query(test.repl->nodes[0], "INSERT INTO t1 VALUES ('STATEMENT')");
    execute_query(test.repl->nodes[0], "SET binlog_format=ROW");
    execute_query(test.repl->nodes[0], "FLUSH LOGS");
    execute_query(test.repl->nodes[0], "INSERT INTO t1 VALUES ('ROW2')");

    // Wait for the avrorouter to process the data
    test.maxscale->start();
    bool found = false;

    for (int i = 0; i < 10 && !found; i++)
    {
        sleep(1);
        found = test.log_matches("Possible STATEMENT or MIXED");
    }

    test.expect(found, "Log does not contain the expected 'Possible STATEMENT or MIXED' error.");

    return test.global_result;
}
