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

    Test->maxscales->execute_maxadmin_command(0, (char *) "set server server1 master");
    Test->maxscales->execute_maxadmin_command(0, (char *) "set server server2 slave");
    Test->maxscales->execute_maxadmin_command(0, (char *) "set server server3 slave");
    Test->maxscales->execute_maxadmin_command(0, (char *) "set server server4 slave");

    Test->maxscales->execute_maxadmin_command(0, (char *) "set server g_server1 master");
    Test->maxscales->execute_maxadmin_command(0, (char *) "set server g_server2 slave");
    Test->maxscales->execute_maxadmin_command(0, (char *) "set server g_server3 slave");
    Test->maxscales->execute_maxadmin_command(0, (char *) "set server g_server4 slave");

    Test->tprintf("Connecting to all MaxScale services\n");
    Test->add_result(Test->maxscales->connect_maxscale(0), "Error connection to Maxscale\n");

    //MYSQL * galera_rwsplit = open_conn(4016, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

    Test->tprintf("executing show status 1000 times\n");

    int ThreadsNum = 25;
    pthread_t thread_v1[ThreadsNum];

    int iret1[ThreadsNum];
    for (i = 0; i < ThreadsNum; i ++)
    {
        iret1[i] = pthread_create(&thread_v1[i], NULL, thread1, NULL);
    }

    create_t1(Test->maxscales->conn_rwsplit[0]);
    for (i = 0; i < iterations; i++)
    {
        Test->set_timeout(200);
        insert_into_t1(Test->maxscales->conn_rwsplit[0], 4);
        printf("i=%d\n", i);
    }
    Test->set_timeout(300);
    for (i = 0; i < ThreadsNum; i ++)
    {
        pthread_join(thread_v1[i], NULL);
    }

    Test->maxscales->close_maxscale_connections(0);
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *thread1( void *ptr )
{
    MYSQL * conn = open_conn(Test->maxscales->rwsplit_port[0] , Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password,
                             Test->ssl);
    MYSQL * g_conn = open_conn(4016 , Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password, Test->ssl);
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

