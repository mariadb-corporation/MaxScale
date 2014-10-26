#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    char server1_id[256];
    char server2_id[256];
    char server1_id_d[256];
    char server2_id_d[256];

    find_status_field(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server server1", (char *) "@@server_id", &server1_id[0]);
    find_status_field(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server server2", (char *) "@@server_id", &server2_id[0]);
    find_status_field(Test->repl->nodes[0], (char *) "select @@server_id;", (char *) "@@server_id", &server1_id_d[0]);
    find_status_field(Test->repl->nodes[0], (char *) "select @@server_id;", (char *) "@@server_id", &server2_id_d[0]);

    printf("server1 ID from Maxscale: \t%s\n", server1_id);
    printf("server1 ID directly from node: \t%s\n", server1_id_d);
    printf("server2 ID from Maxscale: \t%s\n", server2_id);
    printf("server2 ID directly from node: \t%s\n", server2_id_d);

    if ((strcmp(server1_id, server1_id_d) !=0 ) ||
        (strcmp(server2_id, server2_id_d) !=0 ) ) {
        global_result = 1;
        printf("Hints does not work!\n");
    }


    Test->CloseMaxscaleConn();
    Test->repl->CloseConn();

    global_result += CheckMaxscaleAlive();

    return(global_result);
}


