/**
 * @file bug507.cpp regression case for bug 507 ( "rw-split router does not send last_insert_id() to master" )
 *
 * - "CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));",
 * - do INSERT using RWsplit
 * - do "select last_insert_id(), @@server_id" using RWSplit and directly to Master, compare @@server_id
 *
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

const char * sel1 = "select last_insert_id(), @@server_id";

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->repl->connect();
    Test->connect_maxscale();

    if (Test->repl->N < 3) {
        Test->tprintf("There is not enoght nodes for test\n");
        Test->copy_all_logs();
        exit(1);
    }

    Test->tprintf("Creating table\n");  fflush(stdout);
    Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t2; CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));");
    Test->tprintf("Doing INSERTs\n");  fflush(stdout);
    Test->try_query(Test->conn_rwsplit, (char *) "insert into t2 (x) values (1);");

    Test->tprintf("Sleeping to let replication happen\n");
    Test->stop_timeout();
    sleep(10);

    Test->set_timeout(20);
    Test->tprintf("Trying \n");
    char last_insert_id1[1024];
    char last_insert_id2[1024];
    if ( (
             find_field(
                 Test->conn_rwsplit, sel1,
                 "@@server_id", &last_insert_id1[0])
             != 0 ) || (
             find_field(
                 Test->repl->nodes[0], sel1,
                 "@@server_id", &last_insert_id2[0])
             != 0 )) {
        Test->tprintf("@@server_id fied not found!!\n");
        Test->copy_all_logs();
        exit(1);
    } else {
        Test->tprintf("'%s' to RWSplit gave @@server_id %s\n", sel1, last_insert_id1);
        Test->tprintf("'%s' directly to master gave @@server_id %s\n", sel1, last_insert_id2);
        Test->add_result(strcmp(last_insert_id1, last_insert_id2), "last_insert_id() are different depending in which order terms are in SELECT\n");
    }

    Test->close_maxscale_connections();
    Test->repl->close_connections();

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
