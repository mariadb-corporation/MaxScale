/**
 * @file bug634.cpp  regression case for bug 634 ("SHOW SLAVE STATUS in RW SPLITTER is send to master")
 *
 * - execute SHOW SLAVE STATUS and check resut
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    char master_ip[100];

    Test->read_env();
    Test->print_env();

    Test->connect_maxscale();

    for (int i = 0; i < 100; i++) {
        if (find_field(Test->conn_rwsplit, (char *) "SHOW SLAVE STATUS", (char *) "Master_Host", master_ip) != 0) {
            printf("Master_host files is not found in the SHOW SLAVE STATUS reply, probably query went to master\n");
            global_result++;
        }
        if (strcmp(master_ip, Test->repl->IP[0])) {
            printf("Master IP is wrong\n");
            global_result++;
        }
    }

    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(global_result);
}
