/**
 * @file server_weight.cpp Checks if 'weightby' parameter works
 * - use Galera setup, configure Maxscale
 * @verbatim
[RW Split Router]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
weightby=serversize_rws
user=skysql
passwd=skysql

[Read Connection Router]
type=service
router=readconnroute
router_options=synced
servers=server1,server2,server3,server4
weightby=serversize
user=skysql
passwd=skysql

[server1]
type=server
address=###server_IP_1###
port=###server_port_1###
protocol=MySQLBackend
serversize=1
serversize_rws=1

[server2]
type=server
address=###server_IP_2###
port=###server_port_2###
protocol=MySQLBackend
serversize=2
serversize_rws=3000000

[server3]
type=server
address=###server_IP_3###
port=###server_port_3###
protocol=MySQLBackend
serversize=3
serversize_rws=2000000

[server4]
type=server
address=###server_IP_4###
port=###server_port_4###
protocol=MySQLBackend
serversize=0
serversize_rws=1000000
@endverbatim
 * - create 60 connections to ReadConn master
 * - expect: node1 - 10, node2 - 20, node3 - 30, node4 - 0
 * - create 60 connections to RWSplit
 * - expect all connections on only one slave
 * - check error log, it should not contain "Unexpected parameter 'weightby'"
 */

#include <my_config.h>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    int maxscale_conn_num=60;
    MYSQL *conn_read[maxscale_conn_num];
    MYSQL *conn_rwsplit[maxscale_conn_num];
    TestConnections * Test = new TestConnections(argc, argv);
    int i;
    int global_result = 0;

    Test->read_env();
    Test->print_env();
    Test->galera->connect();

    printf("Connecting to ReadConnMaster on %s\n", Test->maxscale_IP);
    for (i=0; i<maxscale_conn_num; i++) {conn_read[i] = Test->open_readconn_master_connection();}

    printf("Sleeping 5 seconds\n");  sleep(5);

    unsigned int conn_num;
    int Nc[4];

    Nc[0] = maxscale_conn_num / 6;
    Nc[1] = maxscale_conn_num / 3;
    Nc[2] = maxscale_conn_num / 2;
    Nc[3] = 0;

    for (i = 0; i < 4; i++) {
        conn_num = get_conn_num(Test->galera->nodes[i], Test->maxscale_IP, (char *) "test");
        printf("connections to node %d: %u (expected: %u)\n", i, conn_num, Nc[i]);
        if ((i<4) && (Nc[i] != conn_num)) {
            global_result++;
            printf("FAILED! Read: Expected number of connections to node %d is %d\n", i, Nc[i]);
        }
    }

    for (i = 0; i < maxscale_conn_num; i++) { mysql_close(conn_read[i]);}

    printf("Connecting to RWSplit on %s\n", Test->maxscale_IP);
    for (i = 0; i < maxscale_conn_num; i++) {conn_rwsplit[i] = Test->open_rwsplit_connection();}

    printf("Sleeping 5 seconds\n");  sleep(5);

    int slave_found = 0;
    for (i = 1; i < Test->galera->N; i++) {
        conn_num = get_conn_num(Test->galera->nodes[i], Test->maxscale_IP, (char *) "test");
        printf("connections to node %d: %u \n", i, conn_num);
        if ((conn_num != 0) && (conn_num != maxscale_conn_num)) {
            global_result++;
            printf("FAILED! one slave has wrong number of connections\n");
        }
        if (conn_num == maxscale_conn_num) {
            if (slave_found != 0) {
                global_result++;
                printf("FAILED! more then one slave have connections\n");
            } else {
                slave_found = i;
            }
        }
    }

    for (i=0; i<maxscale_conn_num; i++) {mysql_close(conn_rwsplit[i]);}
    Test->galera->close_connections();

    global_result += check_log_err((char *) "Unexpected parameter 'weightby'", FALSE);

    Test->copy_all_logs(); return(global_result);
}
