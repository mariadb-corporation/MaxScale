/**
 * @file bug587.cpp  regression case for bug 587 ( "  Hint filter don't work if listed before regex filter in configuration file" )
 *
 * - Maxscale.cnf
 * @verbatim
[hints]
type=filter
module=hintfilter

[regex]
type=filter
module=regexfilter
match=fetch
replace=select

[RW Split Router]
type=service
router= readwritesplit
servers=server1,     server2,              server3,server4
user=skysql
passwd=skysql
max_slave_connections=100%
use_sql_variables_in=all
router_options=slave_selection_criteria=LEAST_BEHIND_MASTER
filters=hints|regex
@endverbatim
 * - check if hints filter working by executing and comparing results:
 *  + via RWSPLIT: "select @@server_id; -- maxscale route to server server%d" (%d - node number)
 *  + directly to backend node "select @@server_id;"
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    char server_id[256];
    char server_id_d[256];

    char hint_sql[64];

    for (int i = 1; i < 25; i++) {
        for (int j = 0; j < Test->repl->N; j++) {

            sprintf(hint_sql, "select @@server_id; -- maxscale route to server server%d", j+1);

            find_status_field(Test->conn_rwsplit, hint_sql, (char *) "@@server_id", &server_id[0]);
            find_status_field(Test->repl->nodes[j], (char *) "select @@server_id;", (char *) "@@server_id", &server_id_d[0]);

            printf("server%d ID from Maxscale: \t%s\n", j+1, server_id);
            printf("server%d ID directly from node: \t%s\n", j+1, server_id_d);

            if (strcmp(server_id, server_id_d) !=0 )  {
                global_result = 1;
                printf("Hints does not work!\n");
            }
        }
    }


    Test->CloseMaxscaleConn();
    Test->repl->CloseConn();

    global_result += CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}


