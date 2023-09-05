/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file schemarouter_duplicate.cpp - Schemarouter duplicate table detection test
 *
 * - Start MaxScale
 * - create DB and table on all nodes
 * - Connect to schemarouter
 * - Execute query and expect failure
 * - Check that message about duplicate tables is logged into error log
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.reset_timeout();

    /** Create a database and table on all nodes */
    test.repl->execute_query_all_nodes("STOP SLAVE");
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS duplicate;");
    test.repl->execute_query_all_nodes("CREATE DATABASE duplicate;");
    test.repl->execute_query_all_nodes("CREATE TABLE duplicate.duplicate (a int, b int);");

    test.maxscale->connect_maxscale();
    test.add_result(execute_query(test.maxscale->conn_rwsplit, "SELECT 1") == 0,
                    "Query should fail when duplicate table is found.");
    sleep(10);
    test.log_includes("Duplicate tables found");
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS duplicate");
    test.repl->execute_query_all_nodes("START SLAVE");
    return test.global_result;
}
