#include <my_config.h>
#include <iostream>
#include "testconnections.h"

const char * sel1 = "select @@wsrep_node_address, last_insert_id();";
const char * sel2 = "select last_insert_id(), @@wsrep_node_address;";

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();
    Test->galera->Connect();
    Test->ConnectMaxscale();

    if (Test->galera->N < 3) {
        printf("There is not enoght nodes for test\n");
        exit(1);
    }

    printf("Creating table\n");  fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t2; CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));");
    printf("Doing INSERTs\n");  fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "insert into t2 (x) values (1);");

    global_result += execute_query(Test->galera->nodes[0], (char *) "insert into t2 (x) values (2);");
    global_result += execute_query(Test->galera->nodes[0], (char *) "insert into t2 (x) values (3);");

    global_result += execute_query(Test->galera->nodes[1], (char *) "insert into t2 (x) values (4);");
    global_result += execute_query(Test->galera->nodes[1], (char *) "insert into t2 (x) values (5);");
    global_result += execute_query(Test->galera->nodes[1], (char *) "insert into t2 (x) values (6);");

    global_result += execute_query(Test->galera->nodes[2], (char *) "insert into t2 (x) values (7);");
    global_result += execute_query(Test->galera->nodes[2], (char *) "insert into t2 (x) values (8);");
    global_result += execute_query(Test->galera->nodes[2], (char *) "insert into t2 (x) values (9);");
    global_result += execute_query(Test->galera->nodes[2], (char *) "insert into t2 (x) values (10);");

    printf("Sleeping to let replication happen\n");  fflush(stdout);
    sleep(10);


    printf("Trying \n");  fflush(stdout);
    char last_insert_id1[1024];
    char last_insert_id2[1024];
    if ( (
             find_status_field(
                 Test->conn_rwsplit, sel1,
                 "last_insert_id()", &last_insert_id1[0])
             != 0 ) || (
             find_status_field(
                 Test->conn_rwsplit, sel2,
                 "last_insert_id()", &last_insert_id2[0])
             != 0 )) {
        printf("last_insert_id() fied not found!!\n");
        exit(1);
    } else {
        printf("'%s' gave last_insert_id() %s\n", sel1, last_insert_id1);
        printf("'%s' gave last_insert_id() %s\n", sel2, last_insert_id2);
        if (strcmp(last_insert_id1, last_insert_id2) !=0 ) {
            global_result++;
            printf("last_insert_id() are different depending in which order terms are in SELECT\n");
        }
    }


    char id_str[1024];
    char str1[1024];

    for (int i = 100; i < 200; i++) {
        sprintf(str1, "insert into t2 (x) values (%d);", i);
        global_result += execute_query(Test->conn_rwsplit, str1);
        sprintf(str1, "select * from t2 where x=%d;", i);
        find_status_field(
                    Test->conn_rwsplit, str1,
                    "id", &id_str[0]);
        find_status_field(
                    Test->conn_rwsplit, sel1,
                    "last_insert_id()", &last_insert_id1[0]);
        printf("last_insert_id is %s, id is %s\n", last_insert_id1, id_str);
        if (strcmp(last_insert_id1, id_str) !=0 ) {
            global_result++;
            printf("last_insert_id is not equil to id\n");
        }
    }

    Test->CloseMaxscaleConn();
    Test->galera->CloseConn();

    global_result += CheckMaxscaleAlive();

    return(global_result);
}
