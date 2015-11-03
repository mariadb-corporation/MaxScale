/**
 * @file schemarouter_duplicate_db.cpp - Schemarouter duplicate database detection test
 *
 * - Start MaxScale
 * - Connect to schemarouter
 * - Execute query and expect failure
 * - Check that message about duplicate databases is logged into error log
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->connect_maxscale();

    /** Create a database on all nodes */
    execute_query(Test->conn_master, "DROP DATABASE IF EXISTS duplicate;");
    execute_query(Test->conn_master, "CREATE DATABASE duplicate;");

    Test->add_result(execute_query(Test->conn_rwsplit, "SELECT 1"), "Query should fail when duplicate database is found.");
    Test->check_log_err((char *) "Duplicate databases found", true);

    Test->copy_all_logs();
    return(Test->global_result);
}
