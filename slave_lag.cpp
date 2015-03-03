/**
 * @file server_lag.cpp  Create high INSERT load to create slave lag and check that Maxscale start routing queries to Master
 *
 * - in Maxscqale.cnf set max_slave_replication_lag=20
 * - in parallel thread execute as many INSERTs as possible
 * - using "select @@server_id;" check that queries go to one of the slave
 * - wait when slave lag > 20 (control lag using maxadmin interface)
 * - check that now queries go to Master
 */

#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "maxadmin_operations.h"

char sql[1000000];

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
void *query_thread( void *ptr );
void *checks_thread( void *ptr);

TestConnections * Test;

int main(int argc, char *argv[])
{

    Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();
    Test->repl->connect();
    Test->connect_rwsplit();

    // connect to the MaxScale server (rwsplit)

    if (Test->conn_rwsplit == NULL ) {
        printf("Can't connect to MaxScale\n");
        exit(1);
    } else {

        create_t1(Test->conn_rwsplit);

        create_insert_string(sql, 50000, 1);
        printf("sql_len=%lu\n", strlen(sql));
        global_result += execute_query(Test->conn_rwsplit, sql);

        pthread_t threads[1000];
        //pthread_t check_thread;
        int  iret[1000];
        //int check_iret;
        int j;
        exit_flag=0;
        /* Create independent threads each of them will execute function */
        for (j=0; j<16; j++) {
            iret[j] = pthread_create( &threads[j], NULL, query_thread, NULL);
        }
        //check_iret = pthread_create( &check_thread, NULL, checks_thread, NULL);

        /*for (j=0; j<16; j++) {
            pthread_join( threads[j], NULL);
        }*/
        //pthread_join(check_thread, NULL);
        execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale max_slave_replication_lag=20");

        char result[1024];
        char server_id[1024];
        char server1_id[1024];
        int res_d;
        int server1_id_d;
        int server_id_d;
        int rounds = 0;
        find_status_field(Test->repl->nodes[0], (char *) "select @@server_id;", (char *) "@@server_id", &server1_id[0]);
        sscanf(server1_id, "%d", &server1_id_d);

        do {
            getMaxadminParam(Test->maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server server2", (char *) "Slave delay:", result);
            sscanf(result, "%d", &res_d);
            printf("server2: %d\n", res_d);
            find_status_field(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale max_slave_replication_lag=20", (char *) "@@server_id", &server_id[0]);
            sscanf(server_id, "%d", &server_id_d);
            printf("%d\n", server_id_d);
            if ((rounds < 10) and (server1_id_d == server_id_d)) {
                printf("Connected to the master!\n");
                global_result++;
            } else {
                printf("Connected to slave\n");
            }
            fflush(stdout);
            rounds++;
        } while (res_d < 21);

        exit_flag = 1;

        if (server1_id_d != server_id_d) {
            printf("Master id is %d\n", server1_id_d);
            printf("Lag is big, but connection is done to server with id %d\n", server_id_d);
            global_result++;
            fflush(stdout);
        } else {
            printf("Connected to master\n");
        }
        // close connections
        Test->close_rwsplit();
    }
    Test->repl->close_connections();

    Test->copy_all_logs(); return(global_result);
}


void *query_thread( void *ptr )
{
    MYSQL * conn;
    conn = open_conn(Test->repl->port[0], Test->repl->IP[0], Test->repl->user_name, Test->repl->password);
    while (exit_flag == 0) {
        execute_query(conn, (char *) "INSERT into t1 VALUES(1, 1)");
    }
    return NULL;
}

void *checks_thread( void *ptr )
{
    char result[1024];
    for (int i = 0; i < 1000; i++) {
        getMaxadminParam(Test->maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server server2", (char *) "Slave delay:", result);
        printf("server2: %s\n", result);
        getMaxadminParam(Test->maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server server3", (char *) "Slave delay:", result);
        printf("server3: %s\n", result);
        getMaxadminParam(Test->maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server server4", (char *) "Slave delay:", result);
        printf("server4: %s\n", result);
    }
    exit_flag = 1;
    return NULL;
}

