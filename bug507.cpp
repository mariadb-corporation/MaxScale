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
    int global_result = 0;
    int i;

    Test->read_env();
    Test->print_env();
    Test->repl->connect();

    Test->connect_maxscale();

    if (Test->repl->N < 3) {
        printf("There is not enoght nodes for test\n");
        exit(1);
    }

    printf("Creating table\n");  fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t2; CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));");
    printf("Doing INSERTs\n");  fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "insert into t2 (x) values (1);");

    printf("Sleeping to let replication happen\n");  fflush(stdout);
    sleep(10);


    printf("Trying \n");  fflush(stdout);
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
        printf("@@server_id fied not found!!\n");
        exit(1);
    } else {
        printf("'%s' to RWSplit gave @@server_id %s\n", sel1, last_insert_id1);
        printf("'%s' directly to master gave @@server_id %s\n", sel1, last_insert_id2);
        if (strcmp(last_insert_id1, last_insert_id2) !=0 ) {
            global_result++;
            printf("last_insert_id() are different depending in which order terms are in SELECT\n");
        }
    }

    Test->close_maxscale_connections();
    Test->repl->close_connections();

    global_result += check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}
