/**
 * @file bug649.cpp regression case for bug 649 ("Segfault using RW Splitter")
 *
 * - Connect to RWSplit
 * - create load on RWSplit (25 threads doing long INSERTs in the loop)
 * - block Mariadb server on Master node by Firewall
 * - unblock Mariadb server
 * - check if Maxscale is alive
 * - reconnect and check if query execution is ok
 */

#include <my_config.h>
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
    pthread_t parall_traffic1[100];
    int check_iret[100];

    Test = new TestConnections(argc, argv);
    Test->set_timeout(100);

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    Test->connect_rwsplit();

    Test->add_result(create_t1(Test->conn_rwsplit), "t1 creation Failed\n");
    create_insert_string(sql, 65000, 1);

    for (int j = 0; j < 25; j++) {
        check_iret[j] = pthread_create( &parall_traffic1[j], NULL, parall_traffic, NULL);
    }

    sleep(1);

    Test->tprintf("Setup firewall to block mysql on master\n");
    Test->repl->block_node(0); fflush(stdout);

    sleep(1);

    Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");fflush(stdout);

    sleep(1);

    Test->tprintf("Setup firewall back to allow mysql\n");
    Test->repl->unblock_node(0); fflush(stdout);
    sleep(10);
    exit_flag = 1;
    sleep(10);

    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive();

    Test->close_rwsplit();

    Test->tprintf("Reconnecting and trying query to RWSplit\n"); fflush(stdout);
    Test->connect_rwsplit();
    Test->try_query(Test->conn_rwsplit, (char *) "show processlist;");
    Test->close_rwsplit();

    exit_flag = 1;
    sleep(10);

    Test->copy_all_logs(); return(Test->global_result);
}


void *parall_traffic( void *ptr )
{
    MYSQL * conn;
    while (exit_flag == 0) {
        conn = Test->open_rwsplit_connection();
        execute_query(conn, sql);
        mysql_close(conn);
        fflush(stdout);
    }
    return NULL;
}
