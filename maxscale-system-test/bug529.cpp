/**
 * @file bug529.cpp regression case for bug 529 ( "'Current no. of conns' not going down" )
 *
 * - create table, opens 50 connections for every router, fill table with data using these connections.
 * - check number of connections to Master - failure if there are more then 100 connections to master.
 * - close RWSptit and ReadConn master connections and check connections to master again.
 * - create 50 ReadConn slave connection in parallel threads, execute "SELECT * FROM t1" ones for every
 *connections, then
 * start using one of connections to create "SELECT" load.
 * - check number of connections to Master again, wait 5 seconds and check number of connections to
 * master ones more time: now expecting 0 connections to master (fail if there is a least one connection to
 *master).
 * - close and reopens all ReadConn slave connections in the main thread and check connections to master again
 * - close all connection in all threads, close parallel thread
 * - do final 'connections to master' check
 */

/*
 *  lisu87 2014-09-08 16:50:29 UTC
 *  After starting maxscale and putting some traffic to it, the number of current connections to master server
 * are still going up:
 *
 *  Server 0x29e6330 (carlsberg)
 *       Server:                         xxx.xxx.xxx.xxx
 *       Status:                         Master, Running
 *       Protocol:                       MySQLBackend
 *       Port:                           3306
 *       Node Id:                        -1
 *       Master Id:                      -1
 *       Repl Depth:                     -1
 *       Number of connections:          58
 *       Current no. of conns:           29
 *       Current no. of operations:      0
 *  Server 0x2948f60 (psy-carslave-1)
 *       Server:                         xxx.xxx.xxx.xxx
 *       Status:                         Slave, Running
 *       Protocol:                       MySQLBackend
 *       Port:                           3306
 *       Node Id:                        -1
 *       Master Id:                      -1
 *       Repl Depth:                     -1
 *       Number of connections:          0
 *       Current no. of conns:           0
 *       Current no. of operations:      0
 *  Server 0x2948e60 (psy-carslave-2)
 *       Server:                         xxx.xxx.xxx.xxx
 *       Status:                         Slave, Running
 *       Protocol:                       MySQLBackend
 *       Port:                           3306
 *       Node Id:                        -1
 *       Master Id:                      -1
 *       Repl Depth:                     -1
 *       Number of connections:          29
 *       Current no. of conns:           0
 *       Current no. of operations:      0
 *  Comment 1 Vilho Raatikka 2014-09-09 06:53:56 UTC
 *  Is the version release-1.0beta?
 *  Does any load cause this or does it require multiple parallel clients, for example?
 *  Comment 2 lisu87 2014-09-09 07:53:18 UTC
 *  The version is release-1.0beta.
 *
 *  Even when just one short connection is made the counter of "Current no. of conns" goes up.
 *
 *  Interesting thing is that the amount of current connections for my slave is always exactly two times
 * smaller than "Number of connections":
 *
 *  Server 0x1f51330 (carlsberg)
 *       Server:                         172.16.76.8
 *       Status:                         Master, Running
 *       Protocol:                       MySQLBackend
 *       Port:                           3306
 *       Node Id:                        -1
 *       Master Id:                      -1
 *       Repl Depth:                     -1
 *       Number of connections:          3278
 *       Current no. of conns:           1639
 *       Current no. of operations:      0
 *  Comment 3 lisu87 2014-09-11 09:54:34 UTC
 *  Any update on this one?
 *  Comment 4 Vilho Raatikka 2014-09-11 10:34:20 UTC
 *  The problem can't be reproduced with the code I'm working currently, and which will be the one where beta
 * release will be refresed from. Thus, I'd wait till beta refresh is done and see if the problem still
 * exists.
 *  Comment 5 lisu87 2014-09-11 10:47:32 UTC
 *  Thank you.
 *
 *  And one more question: is it normal that even if SELECT query has been performed on skave the "Number of
 * connections" counter for master increases too?
 *  Comment 6 Vilho Raatikka 2014-09-11 11:02:08 UTC
 *  (In reply to comment #5)
 *  > Thank you.
 *  >
 *  > And one more question: is it normal that even if SELECT query has been
 *  > performed on skave the "Number of connections" counter for master increases
 *  > too?
 *
 *  When rwsplit listens port 3333 and when a command like :
 *
 *  mysql -h 127.0.0.1 -P 3333 -u maxscaleuser -ppwd -e "select count(user) from mysql.user"
 *
 *  is executed client connects to MaxScale:3333, and MaxScale connects to master and slave(s). So connection
 * count increases in each of those backends despite of query type.
 *
 *  If you already have a rwsplit session, no new connections should be created when new queries are executed.
 *  Comment 7 Vilho Raatikka 2014-09-11 12:34:26 UTC
 *  I built MaxScale from releaes-1.0beta-refresh branch and tested by running 5000 prepared statements in one
 * session to MaxScale/RWSplit and executing 'show servers' in another window. During the run the number of
 * current connections was 1 in each server and after the run all 'current' counters show 0.
 *
 *  If you want me to try with some other use case, describe it and I'll give it a try.
 *  Comment 8 lisu87 2014-09-11 12:45:37 UTC
 *  Thanks, Vilho.
 *
 *  I'm building maxscale from that branch now and will retest shortly.
 *  Comment 9 lisu87 2014-09-11 14:45:26 UTC
 *  Confirmed. It works fine with 1.0beta-refresh.
 *
 *  Thank you!
 *  Comment 10 Vilho Raatikka 2014-09-22 10:11:06 UTC
 *  The problem reappeared later and was eventually fixed in release-1.0beta-refresh commit
 * a41a8d6060c7b60e09686bea8124803f047d85ad
 *
 */

// counting connection to all services


#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;


pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
int conn_N = 50;

TestConnections* Test;

void* parall_traffic(void* ptr);


int main(int argc, char* argv[])
{

    Test = new TestConnections(argc, argv);
    Test->set_timeout(120);
    int i;
    int num_conn = 0;
    char sql[100];

    pthread_t parall_traffic1;

    MYSQL* conn;
    MYSQL* rwsplit_conn[conn_N];
    MYSQL* master_conn[conn_N];
    MYSQL* slave_conn[conn_N];

    Test->repl->connect();

    conn = Test->maxscales->open_rwsplit_connection(0);
    execute_query(conn, (char*) "USE test;");
    create_t1(conn);
    mysql_close(conn);
    Test->tprintf("Table t1 is created\n");

    for (i = 0; i < conn_N; i++)
    {
        Test->set_timeout(60);
        rwsplit_conn[i] = Test->maxscales->open_rwsplit_connection(0);
        master_conn[i] = Test->maxscales->open_readconn_master_connection(0);
        slave_conn[i] = Test->maxscales->open_readconn_slave_connection(0);
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 1);", i);
        execute_query(rwsplit_conn[i], "%s", sql);
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 2);", i);
        execute_query(master_conn[i], "%s", sql);
        fflush(stdout);
    }

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(60);
        num_conn
            = get_conn_num(Test->repl->nodes[i],
                           Test->maxscales->IP[0],
                           Test->maxscales->hostname[0],
                           (char*) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2 * conn_N))
        {
            Test->add_result(1, "too many connections to master\n");
        }
    }

    Test->tprintf("Closing RWSptit and ReadConn master connections\n");
    for (i = 0; i < conn_N; i++)
    {
        mysql_close(rwsplit_conn[i]);
        mysql_close(master_conn[i]);
    }

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(60);
        num_conn
            = get_conn_num(Test->repl->nodes[i],
                           Test->maxscales->IP[0],
                           Test->maxscales->hostname[0],
                           (char*) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2 * conn_N))
        {
            Test->add_result(1, "too many connections to master\n");
        }
    }

    Test->tprintf("Opening more connection to ReadConn slave in parallel thread\n");

    pthread_create(&parall_traffic1, NULL, parall_traffic, NULL);

    for (i = 0; i < Test->repl->N; i++)
    {
        num_conn
            = get_conn_num(Test->repl->nodes[i],
                           Test->maxscales->IP[0],
                           Test->maxscales->hostname[0],
                           (char*) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2 * conn_N))
        {
            Test->add_result(1, "too many connections to master\n");
        }
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping");
    sleep(5);

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(60);
        num_conn
            = get_conn_num(Test->repl->nodes[i],
                           Test->maxscales->IP[0],
                           Test->maxscales->hostname[0],
                           (char*) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0))
        {
            Test->add_result(1, "there are still connections to master\n");
        }
    }

    printf("Closing ReadConn slave connections\n");
    for (i = 0; i < conn_N; i++)
    {
        mysql_close(slave_conn[i]);
    }

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(60);
        num_conn
            = get_conn_num(Test->repl->nodes[i],
                           Test->maxscales->IP[0],
                           Test->maxscales->hostname[0],
                           (char*) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0))
        {
            Test->add_result(1, "there are still connections to master\n");
        }
    }

    Test->tprintf("Opening ReadConn slave connections again\n");
    for (i = 0; i < conn_N; i++)
    {
        Test->set_timeout(60);
        slave_conn[i] = Test->maxscales->open_readconn_slave_connection(0);
        sprintf(sql, "SELECT * FROM t1");
        execute_query(slave_conn[i], "%s", sql);
        fflush(stdout);
    }

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(60);
        num_conn
            = get_conn_num(Test->repl->nodes[i],
                           Test->maxscales->IP[0],
                           Test->maxscales->hostname[0],
                           (char*) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0))
        {
            Test->add_result(1, "there are still connections to master\n");
        }
    }

    Test->tprintf("Closing ReadConn slave connections\n");
    for (i = 0; i < conn_N; i++)
    {
        Test->set_timeout(20);
        mysql_close(slave_conn[i]);
    }
    exit_flag = 1;

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(60);
        num_conn
            = get_conn_num(Test->repl->nodes[i],
                           Test->maxscales->IP[0],
                           Test->maxscales->hostname[0],
                           (char*) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0))
        {
            Test->add_result(1, "there are still connections to master\n");
        }
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}


void* parall_traffic(void* ptr)
{
    MYSQL* slave_conn1[conn_N];
    int i;
    for (i = 0; i < conn_N; i++)
    {
        slave_conn1[i] = Test->maxscales->open_readconn_slave_connection(0);
        execute_query(slave_conn1[i], "SELECT * FROM t1");
    }

    while (exit_flag == 0)
    {
        execute_query(slave_conn1[0], "SELECT * FROM t1");
    }
    for (i = 0; i < conn_N; i++)
    {
        mysql_close(slave_conn1[i]);
    }

    return NULL;
}
