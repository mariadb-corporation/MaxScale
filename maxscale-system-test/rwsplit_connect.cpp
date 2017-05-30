#include <my_config.h>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    Test->repl->connect();

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    Test->connect_rwsplit();

    unsigned int conn_num;
    unsigned int all_conn=0;
    Test->tprintf("Sleeping 5 seconds\n");
    sleep(5);
    Test->tprintf("Checking number of connections ot backend servers\n");
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        Test->tprintf("connections: %u\n", conn_num);
        if ((i == 0) && (conn_num != 1)) {
            Test->add_result(1, " Master should have only 1 connection, but it has %d connection(s)\n", conn_num);
        }
        all_conn += conn_num;
    }
    if (all_conn != 2) {
        Test->add_result(1, "there should be two connections in total: one to master and one to one of slaves, but number of connections is %d\n", all_conn);
    }

    Test->close_rwsplit();
    Test->repl->close_connections();

    Test->copy_all_logs(); return(Test->global_result);
}
