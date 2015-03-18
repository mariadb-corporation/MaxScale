/**
 * @file setup_binlog regression case for bug 705 ("Authentication fails when the user connects to a database when the SQL mode includes ANSI_QUOTES")
 *
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();

    char sys1[1024];
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'rm -rf /usr/local/skysql/maxscale/Binlog_Service/*'", Test->maxscale_sshkey, Test->maxscale_IP);
    printf("%s\n", sys1);  fflush(stdout);
    global_result +=  system(sys1); fflush(stdout);

    Test->repl->start_binlog(Test->maxscale_IP, 5306);

    global_result += executeMaxadminCommand(Test->maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show servers");

    Test->repl->connect();

    create_t1(Test->repl->nodes[0]);
    insert_into_t1(Test->repl->nodes[0], 4);
    select_from_t1(Test->repl->nodes[0], 4);

    Test->repl->close_connections();

    Test->copy_all_logs(); return(global_result);
}


