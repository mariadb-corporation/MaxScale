#include <my_config.h>
#include "testconnections.h"

int main()
{
    int maxscale_conn_num=60;
    MYSQL *conn_read[maxscale_conn_num];
    MYSQL *conn_rwsplit[maxscale_conn_num];
    TestConnections * Test = new TestConnections();
    int i;
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->galera->Connect();

    printf("Connecting to ReadConnMaster on %s\n", Test->Maxscale_IP);
    for (i=0; i<maxscale_conn_num; i++) {conn_read[i] = Test->OpenReadMasterConn();}

    printf("Sleeping 5 seconds\n");  sleep(5);

    unsigned int conn_num;
    int Nc[4];

    Nc[0] = maxscale_conn_num / 6;
    Nc[1] = maxscale_conn_num / 3;
    Nc[2] = maxscale_conn_num / 2;
    Nc[3] = 0;

    for (i = 0; i < 4; i++) {
        conn_num = get_conn_num(Test->galera->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("connections to node %d: %u (expected: %u)\n", i, conn_num, Nc[i]);
        if ((i<4) && (Nc[i] != conn_num)) {
            global_result++;
            printf("FAILED! Read: Expected number of connections to node %d is %d\n", i, Nc[i]);
        }
    }

    for (i = 0; i < maxscale_conn_num; i++) { mysql_close(conn_read[i]);}

    printf("Connecting to RWSplit on %s\n", Test->Maxscale_IP);
    for (i = 0; i < maxscale_conn_num; i++) {conn_rwsplit[i] = Test->OpenRWSplitConn();}

    printf("Sleeping 5 seconds\n");  sleep(5);

    int slave_found = 0;
    for (i = 1; i < Test->galera->N; i++) {
        conn_num = get_conn_num(Test->galera->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("connections to node %d: %u \n", i, conn_num);
        if ((conn_num != 0) && (conn_num != maxscale_conn_num)) {
            global_result++;
            printf("FAILED! one slave has wrong number of connections\n");
        }
        if (conn_num == maxscale_conn_num) {
            if (slave_found != 0) {
                global_result++;
                printf("FAILED! more then one slave have connections");
            } else {
                slave_found = i;
            }
        }
    }

    for (i=0; i<maxscale_conn_num; i++) {mysql_close(conn_rwsplit[i]);}
    Test->galera->CloseConn();

    global_result += CheckLogErr((char *) "Unexpected parameter 'weightby'", FALSE);

    exit(global_result);
}
