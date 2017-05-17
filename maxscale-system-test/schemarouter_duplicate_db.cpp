/**
 * @file schemarouter_duplicate_db.cpp - Schemarouter duplicate database detection test
 *
 * - Start MaxScale
 * - create DB on all nodes (directly via Master)
 * - Connect to schemarouter
 * - Execute query and expect failure
 * - Check that message about duplicate databases is logged into error log
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(30);
    Test->connect_maxscale();

    /** Create a database on all nodes */
    execute_query(Test->conn_master, "DROP DATABASE IF EXISTS duplicate;");
    execute_query(Test->conn_master, "CREATE DATABASE duplicate;");

    Test->add_result(execute_query(Test->conn_rwsplit, "SELECT 1") == 0,
                     "Query should fail when duplicate database is found.");
    Test->stop_timeout();
    sleep(10);
    Test->check_log_err((char *) "Duplicate databases found", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
