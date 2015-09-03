/**
 * @file config_reload_stress.cpp 
 * - connect to RWSplit
 * - in parallel threads start to open/query/close session
 * - change configuration to the replication template and back
 * - check that all services work when replicaton template is loaded
 * - check that RWSplit works with all templates
 * - check Mascale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "big_load.h"

using namespace std;

#define NUM_THREADS 30

static int exit_flag = 0;
static int argc_static;
static char** argv_static;

void *parall_traffic( void *ptr );


int main(int argc, char **argv)
{
    argc_static = argc;
    argv_static = argv;
    TestConnections *Test = new TestConnections(argc, argv);
    int global_result = 0;
    pthread_t parall_traffic1[NUM_THREADS];
    int check_iret[NUM_THREADS];
    int params[NUM_THREADS];
    
    Test->read_env();
    Test->print_env();
    
    Test->repl->connect();
    create_t1(Test->repl->nodes[0]);
    for (int k = 0; k < Test->repl->N; k++) {
        execute_query(Test->repl->nodes[k], (char *) "set global max_connect_errors=1000;");
    }

    Test->repl->close_connections();

    
    for (int j = 0; j < NUM_THREADS; j++) {
        params[j] = j;
        check_iret[j] = pthread_create( &parall_traffic1[j], NULL, parall_traffic, (void*)&params[j]);
    }

    if(!Test->test_maxscale_connections(true, false, false))
        global_result++;

    cout << "Changing configuration..." << endl;
    Test->reconfigure_maxscale((char*)"replication");

    if(!Test->test_maxscale_connections(true, true, true))
        global_result++;

    cout << "Changing configuration..." << endl;
    Test->reconfigure_maxscale((char*)"config_reload");

    if(!Test->test_maxscale_connections(true, false, false))
        global_result++;

    Test->close_maxscale_connections();

    printf("Checking if Maxscale is alive\n");
    global_result += check_maxscale_alive();

    exit_flag = 1;

    for (int j = 0; j < NUM_THREADS; j++) {
        pthread_join(parall_traffic1[j], NULL);
    }

    Test->copy_all_logs(); return(global_result);
}

void *parall_traffic( void *ptr )
{
    int thrnum = *(int*)ptr;
    const char *thrargs[] = {"thrargs", "-s", "-d", NULL};
    TestConnections *Test = new TestConnections(3, (char**)thrargs);
    MYSQL* conn;
    MYSQL_RES* result;

    Test->read_env();
    while (exit_flag == 0)
    {
        if(thrnum < 20)
        {
            conn = Test->open_rwsplit_connection();
            if(thrnum < 2)
                insert_into_t1(conn, 4);
            else
                select_from_t1(conn, 4);
        }
        else if(thrnum < 25)
        {
            conn = Test->open_readconn_master_connection();
            select_from_t1(conn, 4);
        }
        else
        {
            conn = Test->open_readconn_slave_connection();
            select_from_t1(conn, 4);
        }
        mysql_close(conn);
    }
    return NULL;
}

