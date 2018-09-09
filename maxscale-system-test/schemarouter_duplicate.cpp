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
#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.set_timeout(30);

    /** Create a database and table on all nodes */
    test.repl->execute_query_all_nodes("STOP SLAVE");
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS duplicate;");
    test.repl->execute_query_all_nodes("CREATE DATABASE duplicate;");
    test.repl->execute_query_all_nodes("CREATE TABLE duplicate.duplicate (a int, b int);");

    test.maxscales->connect_maxscale(0);
    test.add_result(execute_query(test.maxscales->conn_rwsplit[0], "SELECT 1") == 0,
                    "Query should fail when duplicate table is found.");
    test.stop_timeout();
    sleep(10);
    test.check_log_err(0, (char*) "Duplicate tables found", true);
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS duplicate");
    test.repl->execute_query_all_nodes("START SLAVE");
    return test.global_result;
}
