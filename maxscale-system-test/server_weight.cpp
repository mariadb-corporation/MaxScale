/**
 * Check if 'weightby' parameter works
 *
 * - create 60 connections to ReadConn master
 * - expect: node1 - 10, node2 - 20, node3 - 30, node4 - 0
 * - create 60 connections to RWSplit
 * - expect: node1 - 60, node2 - 30, node3 - 20, node4 - 10
 */


#include "testconnections.h"

void check_conn_num(TestConnections& test, int* Nc)
{
    for (int i = 0; i < 4; i++)
    {
        int conn_num = get_conn_num(test.repl->nodes[i],
                                    test.maxscales->IP[0],
                                    test.maxscales->hostname[0],
                                    (char*) "test");
        test.tprintf("connections to node %d: %u (expected: %u)\n", i, conn_num, Nc[i]);
        if ((i < 4) && (Nc[i] != conn_num))
        {
            test.add_result(1, "Read: Expected number of connections to node %d is %d\n", i, Nc[i]);
        }
    }
}

int main(int argc, char* argv[])
{
    int maxscale_conn_num = 60;
    MYSQL* conn_read[maxscale_conn_num];
    MYSQL* conn_rwsplit[maxscale_conn_num];
    TestConnections test(argc, argv);
    test.set_timeout(30);
    int i;

    test.repl->connect();

    test.tprintf("Connecting to ReadConnMaster on %s\n", test.maxscales->IP[0]);
    for (i = 0; i < maxscale_conn_num; i++)
    {
        // Open the connection and perform a query on it. This way we know it'll
        // be fully established when we count the connections.
        conn_read[i] = test.maxscales->open_readconn_master_connection(0);
        test.try_query(conn_read[i], "SELECT 1");
    }

    int Nc[4];

    Nc[0] = maxscale_conn_num / 6;
    Nc[1] = maxscale_conn_num / 3;
    Nc[2] = maxscale_conn_num / 2;
    Nc[3] = 0;

    test.set_timeout(30);
    check_conn_num(test, Nc);

    for (i = 0; i < maxscale_conn_num; i++)
    {
        mysql_close(conn_read[i]);
    }

    test.set_timeout(30);
    test.tprintf("Connecting to RWSplit on %s\n", test.maxscales->IP[0]);
    for (i = 0; i < maxscale_conn_num; i++)
    {
        conn_rwsplit[i] = test.maxscales->open_rwsplit_connection(0);
        test.try_query(conn_rwsplit[i], "SELECT 1");
    }

    Nc[1] = maxscale_conn_num / 2;
    Nc[2] = maxscale_conn_num / 3;
    Nc[3] = maxscale_conn_num / 6;
    Nc[0] = maxscale_conn_num;

    test.set_timeout(30);
    check_conn_num(test, Nc);


    for (i = 0; i < maxscale_conn_num; i++)
    {
        mysql_close(conn_rwsplit[i]);
    }
    test.repl->close_connections();

    return test.global_result;
}
