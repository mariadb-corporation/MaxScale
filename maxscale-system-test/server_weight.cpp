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


#include "testconnections.h"

void check_conn_num(TestConnections* Test, int * Nc)
{
    for (int i = 0; i < 4; i++)
    {
        int conn_num = get_conn_num(Test->galera->nodes[i], Test->maxscales->IP[0], Test->maxscales->hostname[0],
                                    (char *) "test");
        Test->tprintf("connections to node %d: %u (expected: %u)\n", i, conn_num, Nc[i]);
        if ((i < 4) && (Nc[i] != conn_num))
        {
            Test->add_result(1, "Read: Expected number of connections to node %d is %d\n", i, Nc[i]);
        }
    }
}

int main(int argc, char *argv[])
{
    int maxscale_conn_num = 60;
    MYSQL *conn_read[maxscale_conn_num];
    MYSQL *conn_rwsplit[0][maxscale_conn_num];
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    int i;

    Test->galera->connect();

    Test->tprintf("Connecting to ReadConnMaster on %s\n", Test->maxscales->IP[0]);
    for (i = 0; i < maxscale_conn_num; i++)
    {
        conn_read[i] = Test->maxscales->open_readconn_master_connection(0);
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping 15 seconds\n");
    sleep(15);

    int Nc[4];

    Nc[0] = maxscale_conn_num / 6;
    Nc[1] = maxscale_conn_num / 3;
    Nc[2] = maxscale_conn_num / 2;
    Nc[3] = 0;

    Test->set_timeout(30);
    check_conn_num(Test, Nc);

    for (i = 0; i < maxscale_conn_num; i++)
    {
        mysql_close(conn_read[i]);
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping 15 seconds\n");
    sleep(15);

    Test->set_timeout(30);
    Test->tprintf("Connecting to RWSplit on %s\n", Test->maxscales->IP[0]);
    for (i = 0; i < maxscale_conn_num; i++)
    {
        conn_rwsplit[0][i] = Test->maxscales->open_rwsplit_connection(0);
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping 15 seconds\n");
    sleep(15);

    /** Readwritesplit should always create a connection to the master. For
     * this test we use the priority mechanism to force the first node as
     * the master since Galera clusters don't have a deterministic master node. */
    Nc[1] = maxscale_conn_num / 2;
    Nc[2] = maxscale_conn_num / 3;
    Nc[3] = maxscale_conn_num / 6;
    Nc[0] = maxscale_conn_num;

    Test->set_timeout(30);
    check_conn_num(Test, Nc);


    for (i = 0; i < maxscale_conn_num; i++)
    {
        mysql_close(conn_rwsplit[0][i]);
    }
    Test->galera->close_connections();

    Test->check_log_err(0, (char *) "Unexpected parameter 'weightby'", false);
    Test->check_log_err(0, (char *)
                        "Weighting parameter 'serversize' with a value of 0 for server 'server4' rounds down to zero", true);

    // Pre-1.3.0 failure message
    //Test->check_log_err(0, (char *) "Server 'server4' has no value for weighting parameter 'serversize', no queries will be routed to this server", true);


    int rval = Test->global_result;
    delete Test;
    return rval;
}
