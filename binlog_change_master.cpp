/**
 * @file binlog_change_master.cpp
 * - setup binlog
 * - start thread wich executes transactions
 * - block master
 * - transaction thread tries to elect a new master a continue with new master
 * - continue transaction with new master
 * - stop transactions
 * - wait
 * - chack data on all nodes
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "test_binlog_fnc.h"
#include "big_transaction.h"

void *disconnect_thread( void *ptr );
void *transaction_thread( void *ptr );

TestConnections * Test ;
int exit_flag;
int master = 0;

int transaction(MYSQL * conn, int N)
{
    int local_result = 0;
    char sql[1000000];

    Test->tprintf("START TRANSACTION\n");
    local_result += execute_query(conn, (char *) "START TRANSACTION");
    if (local_result != 0) {Test->tprintf("START TRANSACTION Failed\n"); return(local_result);}
    Test->tprintf("SET\n");
    local_result += execute_query(conn, (char *) "SET autocommit = 0");
    if (local_result != 0) {Test->tprintf("SET Failed\n");return(local_result);}

    create_insert_string(sql, 50000, N);
    Test->tprintf("INSERT\n");
    local_result += execute_query(conn, sql);
    if (local_result != 0) {Test->tprintf("Insert Failed\n");return(local_result);}

    Test->tprintf("COMMIT\n");
    local_result += execute_query(conn, (char *) "COMMIT");
    if (local_result != 0) {Test->tprintf("Commit Failed\n");return(local_result);}
    return(local_result);
}


int main(int argc, char *argv[])
{
     Test = new TestConnections(argc, argv);
    Test->set_timeout(3000);

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);

    Test->repl->connect();
    Test->repl->execute_query_all_nodes((char *) "STOP SLAVE");
    Test->repl->execute_query_all_nodes((char *) "RESET SLAVE ALL");
    Test->repl->execute_query_all_nodes((char *) "RESET MASTER");
    //Test->repl->close_connections();

    Test->tprintf("Starting binlog configuration\n");
    Test->start_binlog();

    pthread_t disconnec_thread_t;
    int  disconnect_iret;
    pthread_t transaction_thread_t;
    int  transaction_iret;

    exit_flag=0;
    Test->tprintf("Starting query thread\n");
    //disconnect_iret = pthread_create( &disconnec_thread_t, NULL, disconnect_thread, NULL);
    transaction_iret = pthread_create( &transaction_thread_t, NULL, transaction_thread, NULL);

    Test->tprintf("Sleeping\n");
    Test->stop_timeout();
    sleep(60);
    Test->tprintf("Blocking master\n");
    Test->repl->block_node(0);
    Test->stop_timeout();
    sleep(2400);

    Test->tprintf("Done! Waiting for thread\n");
    pthread_join(transaction_iret, NULL );
    Test->tprintf("Done!\n");


    Test->copy_all_logs(); return(Test->global_result);
}


/*const char * setup_slave1 =
        "change master to MASTER_HOST='%s',\
         MASTER_USER='repl',\
         MASTER_PASSWORD='repl',\
         MASTER_LOG_FILE='%s',\
         MASTER_LOG_POS=4,\
         MASTER_PORT=%d";
  */
        const char * setup_slave1 =
                "change master to MASTER_HOST='%s',\
                 MASTER_USER='repl',\
                 MASTER_PASSWORD='repl',\
                 MASTER_LOG_FILE='%s',\
                 MASTER_LOG_POS=%s,\
                 MASTER_PORT=%d";


int select_new_master(TestConnections * test)
{
    char log_file[256];
    char log_file_new[256];
    char log_pos[256];

    // Stopping slave
    test->repl->connect();
    test->tprintf("'stop slave' to node2");
    test->try_query(Test->repl->nodes[2], (char *) "stop slave;");
    test->tprintf("'reset slave' to node2");
    test->try_query(Test->repl->nodes[2], (char *) "RESET slave all;");
    //execute_query(Test->repl->nodes[2], (char *) "reset master;");

    // Get master status
    test->tprintf("show master status\n");
    find_field(test->repl->nodes[2], (char *) "show master status", (char *) "File", &log_file[0]);
    find_field(test->repl->nodes[2], (char *) "show master status", (char *) "Position", &log_pos[0]);
    test->tprintf("Real master file: %s\n", log_file);
    test->tprintf("Real master pos : %s\n", log_pos);

    sleep(10);
    test->try_query(test->repl->nodes[2], (char *) "flush logs");
    sleep(10);

    char * p = strchr(log_file, '.') + 1;
    test->tprintf("log file num %s\n", p);
    int pd;
    sscanf(p, "%d", &pd);
    test->tprintf("log file num (d) %d\n", pd);
    p[0] = '\0';
    test->tprintf("log file name %s\n", log_file);
    sprintf(log_file_new, "%s%06d", log_file, pd+1);

    // Set Maxscale to new master
    test->tprintf("Connecting to MaxScale binlog router (with any DB)\n");
    MYSQL * binlog = open_conn_no_db(test->binlog_port, test->maxscale_IP, test->repl->user_name, test->repl->password, test->ssl);

    test->add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    test->try_query(binlog, "stop slave");
    //test->try_query(binlog, "reset slave all");

    sleep(10);

    test->tprintf("configuring Maxscale binlog router\n");

    char str[1024];
    //sprintf(str, setup_slave1, test->repl->IP[2], log_file_new, test->repl->port[2]);
    sprintf(str, setup_slave1, test->repl->IP[2], log_file_new, "4", test->repl->port[2]);
    test->tprintf("change master query: %s\n", str);
    test->try_query(binlog, str);


    sleep(20);

    test->try_query(binlog, "start slave");

    test->repl->close_connections();

}

void *disconnect_thread( void *ptr )
{
    MYSQL * conn;
    char cmd[256];
    int i;
    conn = open_conn(Test->binlog_port, Test->maxscale_IP, Test->repl->user_name, Test->repl->password, Test->repl->ssl);
    Test->add_result(mysql_errno(conn), "Error connecting to Binlog router, error: %s\n", mysql_error(conn));
    i = 3;
    while (exit_flag == 0) {
        sprintf(cmd, "DISCONNECT SERVER %d", i);
        execute_query(conn, cmd);
        i++; if (i > Test->repl->N) {i = 3; sleep(30); execute_query(conn, (char *) "DISCONNECT SERVER ALL");}
        sleep(5);
    }
    return NULL;
}


void *transaction_thread( void *ptr )
{
    MYSQL * conn;
    char cmd[256];
    int trans_result = 0;
    int i = 0;
    conn = open_conn(Test->repl->port[master], Test->repl->IP[master], Test->repl->user_name, Test->repl->password, Test->repl->ssl);
    Test->add_result(mysql_errno(conn), "Error connecting to Binlog router, error: %s\n", mysql_error(conn));
    create_t1(conn);

    while ((exit_flag == 0) && (trans_result == 0))
    {
        trans_result = transaction(conn, i);
        Test->tprintf("Transaction %d\n", i);
        i++;
    }

    i--;
    Test->tprintf("Transaction %d failed\n", i);

    select_new_master(Test);

    master=2;

    conn = open_conn(Test->repl->port[master], Test->repl->IP[master], Test->repl->user_name, Test->repl->password, Test->repl->ssl);
    Test->add_result(mysql_errno(conn), "Error connecting to Binlog router, error: %s\n", mysql_error(conn));

    while (exit_flag == 0)
    {
        if (transaction(conn, i) == 0)
        {
            Test->tprintf("Transaction %d\n", i);
        } else {
            Test->tprintf("Transaction %d FAILED!\n", i);
        }
        i++;
    }

    return NULL;
}

