/**
 * @file bug649.cpp regression case for bug 649 ("Segfault using RW Splitter")
 * @verbatim

[RW_Router]
type=service
router=readconnroute
servers=server1
user=skysql
passwd=skysql
version_string=5.1-OLD-Bored-Mysql
filters=DuplicaFilter

[RW_Split]
type=service
router=readwritesplit
servers=server1,server3,server2
user=skysql
passwd=skysql

[DuplicaFilter]
type=filter
module=tee
service=RW_Split

   @endverbatim
 * - Connect to RWSplit
 * - create load on RWSplit (25 threads doing long INSERTs in the loop)
 * - block Mariadb server on Master node by Firewall
 * - unblock Mariadb server
 * - check if Maxscale is alive
 * - reconnect and check if query execution is ok
 */


#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;

TestConnections * Test ;

char sql[1000000];

void *parall_traffic( void *ptr );

int main(int argc, char *argv[])
{
    int threads_num = 20;
    pthread_t parall_traffic1[threads_num];
    int check_iret[threads_num];

    Test = new TestConnections(argc, argv);
    int time_to_run = (Test->smoke) ? 10 : 30;
    Test->set_timeout(10);

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    Test->connect_rwsplit();

    Test->repl->connect();
    Test->tprintf("Drop t1 if exists\n");
    execute_query(Test->repl->nodes[0], "DROP TABLE IF EXISTS t1;");
    Test->tprintf("Create t1\n");
    Test->add_result(create_t1(Test->repl->nodes[0]), "t1 creation Failed\n");
    Test->repl->close_connections();

    Test->stop_timeout();
    sleep(5);

    create_insert_string(sql, 65000, 1);
    Test->tprintf("Creating query threads\n", time_to_run);
    for (int j = 0; j < threads_num; j++)
    {
        Test->set_timeout(20);
        check_iret[j] = pthread_create(&parall_traffic1[j], NULL, parall_traffic, NULL);
    }

    Test->stop_timeout();
    Test->tprintf("Waiting %d seconds\n", time_to_run);
    sleep(time_to_run);

    Test->tprintf("Setup firewall to block mysql on master\n");
    Test->repl->block_node(0);
    fflush(stdout);

    Test->tprintf("Waiting %d seconds\n", time_to_run);
    sleep(time_to_run);

    Test->set_timeout(30);
    Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
    if (execute_query_silent(Test->conn_rwsplit, (char *) "show processlist;") == 0)
    {
        Test->add_result(1, "Failure is expected, but query is ok\n");
    }

    Test->stop_timeout();
    sleep(time_to_run);

    Test->tprintf("Setup firewall back to allow mysql\n");
    Test->repl->unblock_node(0);
    fflush(stdout);
    Test->stop_timeout();
    sleep(time_to_run);
    exit_flag = 1;
    for (int i = 0; i < threads_num; i++)
    {
        Test->set_timeout(30);
        pthread_join(parall_traffic1[i], NULL);
        Test->tprintf("exit %d\n", i);
    }
    Test->stop_timeout();
    sleep(5);

    Test->set_timeout(20);
    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive();

    Test->set_timeout(20);
    Test->tprintf("Reconnecting to RWSplit ...\n");
    Test->connect_rwsplit();
    Test->tprintf("                        ... and trying query\n");
    Test->try_query(Test->conn_rwsplit, (char *) "show processlist;");
    Test->close_rwsplit();

    /** Clean up */
    Test->repl->connect();
    execute_query(Test->repl->nodes[0], "DROP TABLE IF EXISTS t1;");

    int rval = Test->global_result;
    delete Test;
    return rval;
}


void *parall_traffic( void *ptr )
{
    MYSQL * conn;
    mysql_thread_init();
    conn = Test->open_rwsplit_connection();
    if ((conn != NULL) && (mysql_errno(conn) == 0))
    {
        while (exit_flag == 0)
        {
            execute_query_silent(conn, sql);
            fflush(stdout);
        }
    }
    else
    {
        Test->tprintf("Error opening connection");
    }

    if (conn != NULL )
    {
        mysql_close(conn);
    }
    return NULL;
}
