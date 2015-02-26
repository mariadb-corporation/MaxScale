/**
 * @file bug718.cpp bug718 regression case
 *
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "maxadmin_operations.h"

using namespace std;

TestConnections * Test;
void *thread1( void *ptr );
void *thread2( void *ptr );

int db1_num = 0;
int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;

    Test->PrintIP();

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "set server server1 master");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "set server server2 slave");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "set server server3 slave");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "set server server4 slave");

    printf("Connecting to all MaxScale services\n"); fflush(stdout);
    global_result += Test->ConnectMaxscale();

    //MYSQL * galera_rwsplit = open_conn(4016, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

    printf("executing show status 1000 times\n"); fflush(stdout);

    int ThreadsNum = 25;
    pthread_t thread_v1[ThreadsNum];

    int iret1[ThreadsNum];

    for (i = 0; i < ThreadsNum; i ++) { iret1[i] = pthread_create( &thread_v1[i], NULL, thread1, NULL); }

    create_t1(Test->conn_rwsplit);
    for (i = 0; i < 10000; i++) {
        insert_into_t1(Test->conn_rwsplit, 4);
        printf("i=%d\n", i);
    }

    for (i = 0; i < ThreadsNum; i ++) { pthread_join( thread_v1[i], NULL); }

    Test->CloseMaxscaleConn();
    CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}

void *thread1( void *ptr )
{
    MYSQL * conn = open_conn(Test->rwsplit_port , Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);
    char sql[1034];
    sprintf(sql, "CREATE DATABASE IF NOT EXISTS test%d; USE test%d", db1_num, db1_num);
    execute_query(conn, sql);
    create_t1(conn);
    for (int i = 0; i < 10000; i++) {
        insert_into_t1(conn, 4);
    }

    return NULL;
}

