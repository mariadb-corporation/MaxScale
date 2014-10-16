#include <my_config.h>
#include "testconnections.h"
#include "get_com_select_insert.h"
#include "sql_t1.h"


int selects[256];
int inserts[256];
int new_selects[256];
int new_inserts[256];
int silent = 0;
int tolerance;

char sql[1000000];


pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
void *kill_vm_thread( void *ptr );
void *checks_thread( void *ptr);

TestConnections * Test;

int main()
{
  MYSQL *conn_rwsplit;

  Test = new TestConnections();
  int global_result = 0;
  int i;

  Test->ReadEnv();
  Test->PrintIP();
  Test->repl->Connect();
  Test->ConnectRWSplit();

  tolerance=0;


  // connect to the MaxScale server (rwsplit)

  if (Test->conn_rwsplit == NULL ) {
    printf("Can't connect to MaxScale\n");
    exit(1);
  } else {
 
//    global_result += execute_query(conn_rwsplit, "DROP TABLE IF EXISTS t1;");
//    global_result += execute_query(conn_rwsplit, "create table t1 (x1 int);");
    create_t1(Test->conn_rwsplit);

    create_insert_string(sql, 50000, 1);
    printf("sql_len=%lu\n", strlen(sql));
    global_result += execute_query(Test->conn_rwsplit, sql);

    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);

    pthread_t threads[1000];
    pthread_t check_thread;
    int  iret[1000];
    int check_iret;
    int j;
    exit_flag=0;
    /* Create independent threads each of which will execute function */
     for (j=0; j<10; j++) {
        iret[j] = pthread_create( &threads[j], NULL, kill_vm_thread, NULL);
     }
     check_iret = pthread_create( &check_thread, NULL, checks_thread, NULL);   

     for (j=0; j<10; j++) {
        pthread_join( threads[j], NULL);
     }
     pthread_join(check_thread, NULL);


    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl->N);

  // close connections
    Test->CloseRWSplit();
  }
  Test->repl->CloseConn();

  exit(global_result);
}


void *kill_vm_thread( void *ptr )
{
   MYSQL *conn;
   MYSQL_RES *res;
   MYSQL_ROW row;

  conn = Test->OpenRWSplitConn();
  while (exit_flag == 0) {
      execute_query(conn, sql);	
  }

  mysql_close(conn);
  return NULL;
}

void *checks_thread( void *ptr )
{
    int i;
    int j;
    for (i=0; i<1000000; i++) {
        printf("i=%u\t ", i);
        for (j=0; j < Test->repl->N; j++) {printf("SBM=%u\t", get_Seconds_Behind_Master(Test->repl->nodes[j]));}
        printf("\n");
    }
    exit_flag=1;
    return NULL;
}

