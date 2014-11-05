#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

char sql[1000000];

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
void *query_thread1( void *ptr );
void *query_thread2( void *ptr );
int i1 = 0;
int i2 = 0;



TestConnections * Test;

int load(int *new_inserts, int *new_selects, int *selects, int *inserts, int threads_num)
{
    int global_result;
    Test->repl->Connect();
    Test->ConnectRWSplit();

    // connect to the MaxScale server (rwsplit)

    if (Test->conn_rwsplit == NULL ) {
        printf("Can't connect to MaxScale\n");
        exit(1);
    } else {
        create_t1(Test->conn_rwsplit);
        create_insert_string(sql, 5000, 1);
        global_result += execute_query(Test->conn_rwsplit, sql);
        // close connections
        Test->CloseRWSplit();

        //int threads_num = 100;
        pthread_t thread1[threads_num];
        pthread_t thread2[threads_num];
        //pthread_t check_thread;
        int  iret1[threads_num];
        int  iret2[threads_num];

        printf("COM_INSERT and COM_SELECT before executing test\n");
        get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, 0);
        exit_flag=0;
        /* Create independent threads each of them will execute function */
        for (int i = 0; i < threads_num; i++) {
            iret1[i] = pthread_create( &thread1[i], NULL, query_thread1, NULL);
            iret2[i] = pthread_create( &thread2[i], NULL, query_thread2, NULL);
        }
        printf("Threads are running 100 seconds \n"); fflush(stdout);
        sleep(100);
        exit_flag = 1;
        sleep(1);

        printf("COM_INSERT and COM_SELECT after executing test\n");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, 0);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl->N);
        printf("First thread did %d queries, second - %d \n", i1, i2);
    }
    Test->repl->CloseConn();
    return(global_result);

}

int main()
{

    Test = new TestConnections();
    int global_result = 0;
    int q;

    int selects[256];
    int inserts[256];
    int new_selects[256];
    int new_inserts[256];

    Test->ReadEnv();
    Test->PrintIP();

    global_result += load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 25);

    int avr = (i1 + i2 ) / (Test->repl->N);
    printf("average number of quries per node %d\n", avr);
    int min_q = avr * 0.5;
    int max_q = avr * 1.5;
    printf("Acceplable value for every node from %d until %d\n", min_q, max_q);

    for (int i = 1; i < Test->repl->N; i++) {
        q = selects[i] - new_selects[i];
        if ((q > max_q) || (q < min_q)) {
            printf("FAILED: number of queries for node %d is %d\n", i, q);
            global_result++;
        }
    }

    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100);

    global_result += CheckMaxscaleAlive();
    exit(global_result);
}


void *query_thread1( void *ptr )
{
    MYSQL * conn;
    conn = Test->OpenRWSplitConn();
    while (exit_flag == 0) {
        execute_query(conn, (char *) "SELECT * FROM t1;"); i1++;
    }
    mysql_close(conn);
    return NULL;
}

void *query_thread2( void *ptr )
{
    MYSQL * conn;
    conn = Test->OpenRWSplitConn();
    while (exit_flag == 0) {
        sleep(1);
        execute_query(conn, (char *) "SELECT * FROM t1;"); i2++;
    }
    mysql_close(conn);
    return NULL;
}
