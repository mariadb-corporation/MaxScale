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
 * - second test (bug587_1) is executed with "filters=regex|hints" (dffeent order of filters)
 * - check if hints filter working by executing and comparing results:
 *  + via RWSPLIT: "select @@server_id; -- maxscale route to server server%d" (%d - node number)
 *  + directly to backend node "select @@server_id;"
 * - do the same test with "filters=regex|hints" "filters=hints|regex"
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
    Test->connect_maxscale();

    char server_id[256];
    char server_id_d[256];

    char hint_sql[64];

    for (int i = 1; i < 25; i++) {
        for (int j = 0; j < Test->repl->N; j++) {
            Test->set_timeout(10);
            sprintf(hint_sql, "select @@server_id; -- maxscale route to server server%d", j+1);
            Test->tprintf("%s\n", hint_sql);

            find_field(Test->conn_rwsplit, hint_sql, (char *) "@@server_id", &server_id[0]);
            find_field(Test->repl->nodes[j], (char *) "select @@server_id;", (char *) "@@server_id", &server_id_d[0]);

            Test->tprintf("server%d ID from Maxscale: \t%s\n", j+1, server_id);
            Test->tprintf("server%d ID directly from node: \t%s\n", j+1, server_id_d);

            Test->add_result(strcmp(server_id, server_id_d), "Hints does not work!\n");
        }
    }

    Test->close_maxscale_connections();
    Test->repl->close_connections();

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}


