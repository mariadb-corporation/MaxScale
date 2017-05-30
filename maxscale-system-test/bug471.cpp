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
    Test->set_timeout(10);

    Test->repl->connect();
    Test->add_result(Test->connect_maxscale(), "Failed to connect to MaxScale\n");

    char server_id[256];
    char server_id_d[256];

    char hint_sql[64];

    for (int i = 1; i < 25; i++) {
        for (int j = 0; j < Test->repl->N; j++) {
            if (j !=1 ) {
                Test->set_timeout(5);
                sprintf(hint_sql, "select @@server_id; -- maxscale route to server server%d", j+1);

                find_field(Test->conn_rwsplit, hint_sql, (char *) "@@server_id", &server_id[0]);
                find_field(Test->repl->nodes[j], (char *) "select @@server_id;", (char *) "@@server_id", &server_id_d[0]);

                Test->tprintf("server%d ID from Maxscale: \t%s\n", j+1, server_id);
                Test->tprintf("server%d ID directly from node: \t%s\n", j+1, server_id_d);

                Test->add_result(strcmp(server_id, server_id_d), "Hints does not work!\n");
            }
        }
    }

    Test->set_timeout(10);

    Test->close_maxscale_connections();
    Test->repl->close_connections();

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
