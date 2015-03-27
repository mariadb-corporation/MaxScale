/**
 * @file bug471.cpp bug471 regression case ( Routing Hints route to server sometimes doesn't work )
 *
 * - try "select @@server_id; -- maxscale route to server server%d" (where %d - server number) and compares result
 * with "select @@server_id;" sent directly to backend node.
 * - do it 25 times.
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();
    Test->repl->connect();
    Test->connect_maxscale();

    char server_id[256];
    char server_id_d[256];

    char hint_sql[64];

    for (int i = 1; i < 25; i++) {
        for (int j = 0; j < Test->repl->N; j++) {
            if (j !=1 ) {

                sprintf(hint_sql, "select @@server_id; -- maxscale route to server server%d", j+1);

                find_field(Test->conn_rwsplit, hint_sql, (char *) "@@server_id", &server_id[0]);
                find_field(Test->repl->nodes[j], (char *) "select @@server_id;", (char *) "@@server_id", &server_id_d[0]);

                printf("server%d ID from Maxscale: \t%s\n", j+1, server_id);
                printf("server%d ID directly from node: \t%s\n", j+1, server_id_d);

                if (strcmp(server_id, server_id_d) !=0 )  {
                    global_result = 1;
                    printf("Hints does not work!\n");
                }
            }
        }
    }


    Test->close_maxscale_connections();
    Test->repl->close_connections();

    global_result += check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}



