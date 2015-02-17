/**
 * @file bug718.cpp bug718 regression case
 *
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

TestConnections * Test;
void *thread1( void *ptr );
void *thread2( void *ptr );

int main(int argc, char *argv[])
{
    Test = new TestConnections(argv[0]);
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to all MaxScale services\n"); fflush(stdout);
    global_result += Test->ConnectMaxscale();

    MYSQL * galera_rwsplit = open_conn(4016, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

    printf("executing show status 1000 times\n"); fflush(stdout);


    create_t1(Test->conn_rwsplit);
    insert_into_t1(Test->conn_rwsplit, 4);

    create_t1(galera_rwsplit);
    insert_into_t1(galera_rwsplit, 4);

    Test->CloseMaxscaleConn();

    pthread_t thread_v1;
    pthread_t thread_v2;

    int iret1;
    int iret2;

    iret1 = pthread_create( &thread_v1, NULL, thread1, NULL);
    iret2 = pthread_create( &thread_v2, NULL, thread2, NULL);


    for (i = 0; i < 10000; i++) {
        insert_into_t1(Test->conn_rwsplit, 4);
        printf("i=%d\n", i);
    }

    pthread_join( thread_v1, NULL);
    pthread_join( thread_v2, NULL);

    CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}

void *thread1( void *ptr )
{
    MYSQL * conn = open_conn(Test->rwsplit_port , Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);
    execute_query(conn, "CREATE DATABASE IF NOT EXISTS test1; USE test1");
    create_t1(conn);
    for (int i = 0; i < 10000; i++) {
        insert_into_t1(conn, 4);
    }

    return NULL;
}

void *thread2( void *ptr )
{
    MYSQL * conn = open_conn(4016, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);
    execute_query(conn, "CREATE DATABASE IF NOT EXISTS test1; USE test2");
    create_t1(conn);
    for (int i = 0; i < 10000; i++) {
        insert_into_t1(conn, 4);
    }

    return NULL;
}
