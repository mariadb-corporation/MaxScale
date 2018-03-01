/**
 * @file bug718.cpp bug718 (MXS-19) regression case REMOVED FROM TEST SUITE!! (because manuall Master setting breaks backend)
 * trying to execute INSERTS from several paralell threads when monitors are disabled
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "maxadmin_operations.h"

using namespace std;

TestConnections * Test;
void *thread1( void *ptr );
//void *thread2( void *ptr );

int iterations;

int db1_num = 0;
int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    iterations =  (Test->smoke) ? 20 : 100;
    Test->set_timeout(20);
    int i;

    Test->execute_maxadmin_command((char *) "set server server1 master");
    Test->execute_maxadmin_command((char *) "set server server2 slave");
    Test->execute_maxadmin_command((char *) "set server server3 slave");
    Test->execute_maxadmin_command((char *) "set server server4 slave");

    Test->execute_maxadmin_command((char *) "set server g_server1 master");
    Test->execute_maxadmin_command((char *) "set server g_server2 slave");
    Test->execute_maxadmin_command((char *) "set server g_server3 slave");
    Test->execute_maxadmin_command((char *) "set server g_server4 slave");

    Test->tprintf("Connecting to all MaxScale services\n");
    Test->add_result(Test->connect_maxscale(), "Error connection to Maxscale\n");

    //MYSQL * galera_rwsplit = open_conn(4016, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

    Test->tprintf("executing show status 1000 times\n");

    int ThreadsNum = 25;
    pthread_t thread_v1[ThreadsNum];

    int iret1[ThreadsNum];
    for (i = 0; i < ThreadsNum; i ++)
    {
        iret1[i] = pthread_create(&thread_v1[i], NULL, thread1, NULL);
    }

    create_t1(Test->conn_rwsplit);
    for (i = 0; i < iterations; i++)
    {
        Test->set_timeout(200);
        insert_into_t1(Test->conn_rwsplit, 4);
        printf("i=%d\n", i);
    }
    Test->set_timeout(300);
    for (i = 0; i < ThreadsNum; i ++)
    {
        pthread_join(thread_v1[i], NULL);
    }

    Test->close_maxscale_connections();
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *thread1( void *ptr )
{
    MYSQL * conn = open_conn(Test->rwsplit_port , Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password,
                             Test->ssl);
    MYSQL * g_conn = open_conn(4016 , Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);
    char sql[1034];

    sprintf(sql, "CREATE DATABASE IF NOT EXISTS test%d;", db1_num);
    execute_query(conn, sql);
    sprintf(sql, "USE test%d", db1_num);
    execute_query(conn, sql);

    create_t1(conn);
    create_t1(g_conn);
    for (int i = 0; i < iterations; i++)
    {
        insert_into_t1(conn, 4);
        insert_into_t1(g_conn, 4);
        if ((i / 100) * 100 == i)
        {
            printf("Iteration %d\n", i);
            fflush(stdout);
        }
    }
    return NULL;
}

