/**
 * @file rwsplit_readonly.cpp Testing of the read-only mode for readwritesplit when master fails
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include "testconnections.h"
#include "maxadmin_operations.h"

void test_all_ok(TestConnections *Test)
{
   /** Insert should work */
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_rwsplit, "INSERT INTO test.readonly VALUES (1)"),
                     "Query to service with 'fail_instantly' should succeed");
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_master, "INSERT INTO test.readonly VALUES (1)"),
                     "Query to service with 'fail_on_write' should succeed");
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_slave, "INSERT INTO test.readonly VALUES (1)"),
                     "Query to service with 'error_on_write' should succeed");
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_rwsplit, "SELECT * FROM test.readonly"),
                     "Query to service with 'fail_instantly' should succeed");
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_master, "SELECT * FROM test.readonly"),
                     "Query to service with 'fail_on_write' should succeed");
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_slave, "SELECT * FROM test.readonly"),
                     "Query to service with 'error_on_write' should succeed");
}

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);

    /** Prepare for tests */
    Test->stop_timeout();
    Test->connect_maxscale();
    execute_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS test.readonly");
    execute_query(Test->conn_rwsplit, "CREATE TABLE test.readonly(id int)");

    /** Check that everything is OK before blocking the master */
    test_all_ok(Test);

    /** Block master */
    Test->stop_timeout();
    Test->repl->block_node(0);
    sleep(10);

    /** Select to service with 'fail_instantly' should close the connection */
    Test->set_timeout(30);
    Test->add_result(!execute_query(Test->conn_rwsplit, "SELECT * FROM test.readonly"),
                     "Query to service with 'fail_instantly' should fail");

    /** Other services should still work */
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_master, "SELECT * FROM test.readonly"),
                     "Query to service with 'fail_on_write' should succeed");
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_slave, "SELECT * FROM test.readonly"),
                     "Query to service with 'error_on_write' should succeed");

    /** Insert to 'fail_on_write' should fail and close the connection */
    Test->set_timeout(30);
    Test->add_result(!execute_query(Test->conn_master, "INSERT INTO test.readonly VALUES (1)"),
                     "Query to service with 'fail_on_write' should succeed");
    Test->set_timeout(30);
    Test->add_result(!execute_query(Test->conn_master, "SELECT * FROM test.readonly"),
                     "Query to service with 'fail_on_write' should succeed");

    /** Insert to 'error_on_write' should fail but subsequent SELECTs should work */
    Test->set_timeout(30);
    Test->add_result(!execute_query(Test->conn_slave, "INSERT INTO test.readonly VALUES (1)"),
                     "Query to service with 'error_on_write' should succeed");
    Test->add_result(execute_query(Test->conn_slave, "SELECT * FROM test.readonly"),
                     "Query to service with 'fail_on_write' should succeed");

    /** Close connections and try to create new ones */
    Test->set_timeout(30);
    Test->close_maxscale_connections();
    Test->add_result(Test->connect_rwsplit() != 0, "Connection to 'fail_instantly' service should fail");
    Test->add_result(Test->connect_readconn_master() == 0, "Connection to 'fail_on_write' service should succeed");
    Test->add_result(Test->connect_readconn_slave() == 0, "Connection to 'error_on_write' service should succeed");


    /** The {fail|error}_on_write services should work and allow reads */
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_master, "SELECT * FROM test.readonly"),
                     "Query to service with 'fail_on_write' should succeed");
    Test->set_timeout(30);
    Test->add_result(execute_query(Test->conn_slave, "SELECT * FROM test.readonly"),
                     "Query to service with 'error_on_write' should succeed");

    Test->close_maxscale_connections();
    Test->stop_timeout();
    Test->repl->unblock_node(0);
    sleep(10);

    /** Check that everything is OK after unblocking */
    Test->connect_maxscale();
    test_all_ok(Test);

    /** Clean up test environment */
    Test->repl->flush_hosts();
    Test->copy_all_logs();
    return(Test->global_result);
}
