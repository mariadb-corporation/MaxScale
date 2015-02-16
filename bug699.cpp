/**
 * @file bug699.cpp regression case for bug 699 ( "rw-split sensitive to order of terms in field list of SELECT (round 2)" )
 *
 * - campare @@hostname from "select  @@wsrep_node_name, @@hostname" and "select  @@hostname, @@wsrep_node_name"
 * - comapre @@server_id from "select  @@wsrep_node_name, @@server_id" and "select  @@server_id, @@wsrep_node_name"
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

const char * sel1 = "select  @@wsrep_node_name, @@hostname";
const char * sel2 = "select  @@hostname, @@wsrep_node_name";

const char * sel3 = "select  @@wsrep_node_name, @@server_id";
const char * sel4 = "select  @@server_id, @@wsrep_node_name";

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();

    printf("Trying \n");  fflush(stdout);

    char serverid1[1024];
    char serverid2[1024];

    if ( (
             find_status_field(
                 Test->conn_rwsplit, sel3,
                 "@@server_id", &serverid1[0])
             != 0 ) || (
             find_status_field(
                 Test->conn_rwsplit, sel4,
                 "@@server_id", &serverid2[0])
             != 0 )) {
        printf("@@server_id field not found!!\n");
        exit(1);
    } else {
        printf("'%s' to RWSplit gave @@server_id %s\n", sel3, serverid1);
        printf("'%s' directly to master gave @@server_id %s\n", sel4, serverid2);
        if (strcmp(serverid1, serverid2) !=0 ) {
            global_result++;
            printf("server_id are different depending in which order terms are in SELECT\n");
        }
    }

    if ( (
             find_status_field(
                 Test->conn_rwsplit, sel1,
                 "@@hostname", &serverid1[0])
             != 0 ) || (
             find_status_field(
                 Test->conn_rwsplit, sel2,
                 "@@hostname", &serverid2[0])
             != 0 )) {
        printf("@@hostname field not found!!\n");
        exit(1);
    } else {
        printf("'%s' to RWSplit gave @@hostname %s\n", sel1, serverid1);
        printf("'%s' to RWSplit gave @@hostname %s\n", sel2, serverid2);
        if (strcmp(serverid1, serverid2) !=0 ) {
            global_result++;
            printf("hostname are different depending in which order terms are in SELECT\n");
        }
    }

    Test->CloseMaxscaleConn();

    global_result += CheckMaxscaleAlive();

    return(global_result);
}
