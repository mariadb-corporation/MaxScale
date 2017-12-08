/**
 * @file rwsplit_readonly.cpp Test of the read-only mode for readwritesplit when master fails
 * - check INSERTs via RWSplit
 * - block Master
 * - check SELECT and INSERT with -- fail_instantly, -- error_on_write, -- fail_on_write
 */


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
    Test->tprintf("Testing that writes and reads to all services work\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_rwsplit[0],
                                          "INSERT INTO test.readonly VALUES (1) -- fail_instantly"),
                     "Query to service with 'fail_instantly' should succeed\n");
    Test->set_timeout(30);
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "INSERT INTO test.readonly VALUES (1) -- fail_on_write"),
                     "Query to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "INSERT INTO test.readonly VALUES (1) -- error_on_write"),
                     "Query to service with 'error_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->add_result(execute_query_silent(Test->maxscales->conn_rwsplit[0],
                                          "SELECT * FROM test.readonly -- fail_instantly"),
                     "Query to service with 'fail_instantly' should succeed\n");
    Test->set_timeout(30);
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "SELECT * FROM test.readonly -- fail_on_write"),
                     "Query to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "Query to service with 'error_on_write' should succeed\n");
}

void test_basic(TestConnections *Test)
{
    /** Check that everything is OK before blocking the master */
    Test->maxscales->connect_maxscale(0);
    test_all_ok(Test);

    /** Block master */
    Test->stop_timeout();
    Test->repl->block_node(0);
    sleep(10);

    /** Select to service with 'fail_instantly' should close the connection */
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_instantly'\n");
    Test->add_result(!execute_query_silent(Test->maxscales->conn_rwsplit[0],
                                           "SELECT * FROM test.readonly -- fail_instantly"),
                     "SELECT to service with 'fail_instantly' should fail\n");

    /** Other services should still work */
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'error_on_write' should succeed\n");

    /** Insert to 'fail_on_write' should fail and close the connection */
    Test->set_timeout(30);
    Test->tprintf("INSERT to 'fail_on_write'\n");
    Test->add_result(!execute_query_silent(Test->maxscales->conn_master[0],
                                           "INSERT INTO test.readonly VALUES (1) -- fail_on_write"),
                     "INSERT to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(!execute_query_silent(Test->maxscales->conn_master[0],
                                           "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should fail after an INSERT\n");

    /** Insert to 'error_on_write' should fail but subsequent SELECTs should work */
    Test->set_timeout(30);
    Test->tprintf("INSERT to 'error_on_write'\n");
    Test->add_result(!execute_query_silent(Test->maxscales->conn_slave[0],
                                           "INSERT INTO test.readonly VALUES (1) -- error_on_write"),
                     "INSERT to service with 'error_on_write' should fail\n");
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'fail_on_write' should succeed after an INSERT\n");

    /** Close connections and try to create new ones */
    Test->set_timeout(30);
    Test->maxscales->close_maxscale_connections(0);
    Test->tprintf("Opening connections while master is blocked\n");
    Test->add_result(Test->maxscales->connect_rwsplit(0) == 0,
                     "Connection to 'fail_instantly' service should fail\n");
    Test->add_result(Test->maxscales->connect_readconn_master(0) != 0,
                     "Connection to 'fail_on_write' service should succeed\n");
    Test->add_result(Test->maxscales->connect_readconn_slave(0) != 0,
                     "Connection to 'error_on_write' service should succeed\n");


    /** The {fail|error}_on_write services should work and allow reads */
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'error_on_write' should succeed\n");

    Test->maxscales->close_maxscale_connections(0);
    Test->stop_timeout();
    Test->repl->unblock_node(0);
    sleep(10);

    /** Check that everything is OK after unblocking */
    Test->maxscales->connect_maxscale(0);
    test_all_ok(Test);
    Test->maxscales->close_maxscale_connections(0);
}

void test_complex(TestConnections *Test)
{
    /** Check that everything works before test */
    Test->maxscales->connect_maxscale(0);
    test_all_ok(Test);

    /** Block master */
    Test->stop_timeout();
    Test->repl->block_node(0);
    sleep(10);

    /** Select to service with 'fail_instantly' should close the connection */
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_instantly'\n");
    Test->add_result(!execute_query_silent(Test->maxscales->conn_rwsplit[0],
                                           "SELECT * FROM test.readonly -- fail_instantly"),
                     "SELECT to service with 'fail_instantly' should fail\n");

    /** The {fail|error}_on_write services should allow reads */
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'error_on_write' should succeed\n");

    /** Unblock node and try to read */
    Test->stop_timeout();
    Test->repl->unblock_node(0);
    sleep(10);

    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'error_on_write' should succeed\n");

    /** Block slaves */
    Test->stop_timeout();
    Test->maxscales->close_maxscale_connections(0);
    Test->repl->block_node(1);
    Test->repl->block_node(2);
    Test->repl->block_node(3);
    sleep(20);

    /** Reconnect to MaxScale */
    Test->set_timeout(30);
    Test->maxscales->connect_maxscale(0);

    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'error_on_write' should succeed\n");

    Test->stop_timeout();
    Test->repl->unblock_node(1);
    Test->repl->unblock_node(2);
    Test->repl->unblock_node(3);
    sleep(10);


    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_master[0],
                                          "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should succeed\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(execute_query_silent(Test->maxscales->conn_slave[0],
                                          "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'error_on_write' should succeed\n");

    /** Block all nodes */
    Test->stop_timeout();
    Test->repl->block_node(0);
    Test->repl->block_node(1);
    Test->repl->block_node(2);
    Test->repl->block_node(3);
    sleep(10);

    /** SELECTs should fail*/
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'fail_on_write'\n");
    Test->add_result(!execute_query_silent(Test->maxscales->conn_master[0],
                                           "SELECT * FROM test.readonly -- fail_on_write"),
                     "SELECT to service with 'fail_on_write' should fail\n");
    Test->set_timeout(30);
    Test->tprintf("SELECT to 'error_on_write'\n");
    Test->add_result(!execute_query_silent(Test->maxscales->conn_slave[0],
                                           "SELECT * FROM test.readonly -- error_on_write"),
                     "SELECT to service with 'error_on_write' should fail\n");
    Test->stop_timeout();
    Test->repl->unblock_node(0);
    Test->repl->unblock_node(1);
    Test->repl->unblock_node(2);
    Test->repl->unblock_node(3);
    sleep(10);

    /** Reconnect and check that everything works after the test */
    Test->maxscales->close_maxscale_connections(0);
    Test->maxscales->connect_maxscale(0);
    test_all_ok(Test);
    Test->maxscales->close_maxscale_connections(0);
}

int main(int argc, char *argv[])
{

    TestConnections *Test = new TestConnections(argc, argv);

    /** Prepare for tests */
    Test->stop_timeout();
    Test->maxscales->connect_maxscale(0);
    execute_query_silent(Test->maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS test.readonly\n");
    execute_query_silent(Test->maxscales->conn_rwsplit[0], "CREATE TABLE test.readonly(id int)\n");
    Test->maxscales->close_maxscale_connections(0);

    /** Basic tests */
    test_basic(Test);

    /** More complex tests */
    test_complex(Test);

    /** Clean up test environment */
    Test->repl->flush_hosts();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
