/**
 * @file binlog_change_master.cpp In the binlog router setup stop Master and promote one of the Slaves to be
 * new Master
 * - setup binlog
 * - start thread wich executes transactions
 * - block master
 * - transaction thread tries to elect a new master a continue with new master
 * - continue transaction with new master
 * - stop transactions
 * - wait
 * - chack data on all nodes
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "test_binlog_fnc.h"
#include "big_transaction.h"

void* disconnect_thread(void* ptr);
void* transaction_thread(void* ptr);

TestConnections* Test;
int exit_flag;
int master = 0;
int i_trans = 0;
const int trans_max = 300;
int failed_transaction_num = 0;

/** The amount of rows each transaction inserts */
const int N_INSERTS = 100;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

bool sync_servers(MYSQL* master, MYSQL* slave)
{
    bool rval = false;
    int t = 240;

    for (int i = 0; i < t; i++)
    {
        char master_log_file[80] = "";
        char master_log_pos[80] = "";
        char slave_log_file[80] = "";
        char slave_log_pos[80] = "";
        find_field(master, "SHOW MASTER STATUS", "File", master_log_file);
        find_field(master, "SHOW MASTER STATUS", "Position", master_log_pos);
        find_field(slave, "SHOW SLAVE STATUS", "Master_Log_File", slave_log_file);
        find_field(slave, "SHOW SLAVE STATUS", "Read_Master_Log_Pos", slave_log_pos);

        if (strcmp(slave_log_file, master_log_file) == 0 && strcmp(slave_log_pos, master_log_pos) == 0)
        {
            rval = true;
            break;
        }
        else
        {
            sleep(1);
        }
    }

    if (!rval)
    {
        printf("WARNING: Slave has not caught up in %d seconds. Test will most likely fail.\n", t);
    }

    return rval;
}

int transaction(MYSQL* conn, int N)
{
    int local_result = 0;
    char sql[1000000];

    local_result += execute_query(conn, (char*) "START TRANSACTION");
    if (local_result != 0)
    {
        Test->tprintf("START TRANSACTION Failed\n");
        return local_result;
    }
    local_result += execute_query(conn, (char*) "SET autocommit = 0");
    if (local_result != 0)
    {
        Test->tprintf("SET Failed\n");
        return local_result;
    }

    create_insert_string(sql, N_INSERTS, N);
    local_result += execute_query(conn, "%s", sql);
    if (local_result != 0)
    {
        Test->tprintf("Insert Failed\n");
        return local_result;
    }

    local_result += execute_query(conn, (char*) "COMMIT");
    if (local_result != 0)
    {
        Test->tprintf("Commit Failed\n");
        return local_result;
    }
    return local_result;
}


int main(int argc, char* argv[])
{
    int j;

    Test = new TestConnections(argc, argv);
    Test->set_timeout(3000);

    if (strcmp(Test->test_name, "binlog_change_master_gtid") == 0)
    {
        Test->binlog_master_gtid = true;
        Test->binlog_slave_gtid = true;
        Test->tprintf("Using GTID\n");
    }

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char*) "DROP TABLE IF EXISTS t1;");
    Test->repl->sync_slaves();
    Test->repl->close_connections();
    sleep(5);

    Test->repl->connect();
    Test->repl->execute_query_all_nodes((char*) "STOP SLAVE");
    Test->repl->execute_query_all_nodes((char*) "RESET SLAVE ALL");
    Test->repl->execute_query_all_nodes((char*) "RESET MASTER");

    Test->tprintf("Starting binlog configuration\n");
    Test->start_binlog(0);

    pthread_t transaction_thread_t;

    exit_flag = 0;
    Test->tprintf("Starting query thread\n");

    pthread_create(&transaction_thread_t, NULL, transaction_thread, NULL);

    Test->tprintf("Sleeping\n");
    Test->stop_timeout();

    Test->repl->connect();
    int flushes = Test->smoke ? 2 : 5;
    for (j = 0; j < flushes; j++)
    {
        Test->tprintf("Flush logs on master\n");
        execute_query(Test->repl->nodes[0], (char*) "flush logs");
        sleep(15);
    }

    sleep(15);

    pthread_mutex_lock(&mutex);

    sync_servers(Test->repl->nodes[0], Test->repl->nodes[3]);
    Test->tprintf("Blocking master");
    Test->repl->block_node(0);

    pthread_mutex_unlock(&mutex);

    for (int i = 0; i < 180 && exit_flag == 0; i++)
    {
        sleep(i);
    }

    Test->tprintf("Done! Waiting for thread\n");
    exit_flag = 1;
    pthread_join(transaction_thread_t, NULL);
    Test->tprintf("Done!\n");
    Test->tprintf("Checking data on the node3 (slave)\n");
    char sql[256];
    char rep[256];
    int rep_d;

    Test->repl->connect();
    sync_servers(Test->repl->nodes[2], Test->repl->nodes[3]);

    for (int i_n = 3; i_n < Test->repl->N; i_n++)
    {
        for (j = 0; j < i_trans; j++)
        {
            sprintf(sql, "select count(*) from t1 where fl=%d;", j);
            find_field(Test->repl->nodes[i_n], sql, (char*) "count(*)", rep);
            sscanf(rep, "%d", &rep_d);

            if ((rep_d != N_INSERTS) && (j != (failed_transaction_num - 1)))
            {
                Test->add_result(1, "Transaction %d did not put data into slave\n", j);
            }
            if ((j == (failed_transaction_num - 1)) && (rep_d != 0) && (rep_d != N_INSERTS))
            {
                Test->add_result(1, "Incomplete transaction detected - %d\n", j);
            }
            if ((j == (failed_transaction_num - 1) && rep_d == 0))
            {
                Test->tprintf("Transaction %d was rejected, OK\n", j);
            }
        }
    }
    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}


const char* setup_slave1
    =
        "change master to MASTER_HOST='%s',\
                 MASTER_USER='repl',\
                 MASTER_PASSWORD='repl',\
                 MASTER_LOG_FILE='%s',\
                 MASTER_LOG_POS=%s,\
                 MASTER_PORT=%d";
const char* setup_slave_gtid
    =
        "change master to MASTER_HOST='%s',\
                     MASTER_USER='repl',\
                     MASTER_PASSWORD='repl',\
                     MASTER_PORT=%d, \
                     MASTER_USE_GTID=Slave_pos";


void select_new_master(TestConnections* test)
{
    char log_file[256];
    char log_pos[256];

    char maxscale_log_file[256];
    char maxscale_log_file_new[256];
    char maxscale_log_pos[256];

    // Stopping slave
    test->tprintf("Connection to backend\n");
    test->repl->connect();
    test->tprintf("'stop slave' to node2\n");
    test->try_query(Test->repl->nodes[2], (char*) "stop slave;");
    test->tprintf("'reset slave all' to node2\n");
    test->try_query(Test->repl->nodes[2], (char*) "RESET slave all;");
    // execute_query(Test->repl->nodes[2], (char *) "reset master;");

    // Get master status
    test->tprintf("show master status\n");
    find_field(test->repl->nodes[2], (char*) "show master status", (char*) "File", &log_file[0]);
    find_field(test->repl->nodes[2], (char*) "show master status", (char*) "Position", &log_pos[0]);
    test->tprintf("Real master file: %s\n", log_file);
    test->tprintf("Real master pos : %s\n", log_pos);

    test->tprintf("Connecting to MaxScale binlog router (with any DB)\n");
    MYSQL* binlog = open_conn_no_db(test->maxscales->binlog_port[0],
                                    test->maxscales->IP[0],
                                    test->repl->user_name,
                                    test->repl->password,
                                    test->ssl);
    test->add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    test->tprintf("show master status on maxscale\n");
    find_field(binlog, (char*) "show master status", (char*) "File", &maxscale_log_file[0]);
    find_field(binlog, (char*) "show master status", (char*) "Position", &maxscale_log_pos[0]);

    if (!maxscale_log_file[0] || !maxscale_log_pos[0])
    {
        test->add_result(1, "Failed to query for master status");
        return;
    }

    test->tprintf("Real master file: %s\n", maxscale_log_file);
    test->tprintf("Real master pos : %s\n", maxscale_log_pos);

    char* p = strchr(maxscale_log_file, '.') + 1;
    test->tprintf("log file num %s\n", p);
    int pd;
    sscanf(p, "%d", &pd);
    test->tprintf("log file num (d) %d\n", pd);
    p[0] = '\0';
    test->tprintf("log file name %s\n", maxscale_log_file);
    sprintf(maxscale_log_file_new, "%s%06d", maxscale_log_file, pd + 1);

    test->tprintf("Flush logs %d times\n", pd + 1);
    for (int k = 0; k < pd + 1; k++)
    {
        test->try_query(test->repl->nodes[2], (char*) "flush logs");
    }

    // Set Maxscale to new master
    test->try_query(binlog, "stop slave");
    test->tprintf("configuring Maxscale binlog router\n");


    test->tprintf("reconnect to binlog\n");
    mysql_close(binlog);
    binlog = open_conn_no_db(test->maxscales->binlog_port[0],
                             test->maxscales->IP[0],
                             test->repl->user_name,
                             test->repl->password,
                             test->ssl);
    test->add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    char str[1024];

    if (test->binlog_master_gtid)
    {
        test->tprintf("Configuring new master with GTID\n");
        sprintf(str, setup_slave_gtid, test->repl->IP[2], test->repl->port[2]);
    }
    else
    {
        test->tprintf("Configuring new master with FILE and POS\n");
        // sprintf(str, setup_slave1, test->repl->IP[2], log_file_new, test->repl->port[2]);
        sprintf(str, setup_slave1, test->repl->IP[2], maxscale_log_file_new, "4", test->repl->port[2]);
    }
    test->tprintf("change master query: %s\n", str);
    test->try_query(binlog, "%s", str);
    test->tprintf("start slave\n");
    test->try_query(binlog, "start slave");
    test->tprintf("start slave one more\n");
    test->try_query(binlog, "start slave");
    test->tprintf("slave started!\n");

    test->repl->close_connections();
}

void* disconnect_thread(void* ptr)
{
    MYSQL* conn;
    char cmd[256];
    int i;
    conn = open_conn(Test->maxscales->binlog_port[0],
                     Test->maxscales->IP[0],
                     Test->repl->user_name,
                     Test->repl->password,
                     Test->repl->ssl);
    Test->add_result(mysql_errno(conn), "Error connecting to Binlog router, error: %s\n", mysql_error(conn));
    i = 3;
    while (exit_flag == 0)
    {
        sprintf(cmd, "DISCONNECT SERVER %d", i);
        execute_query(conn, "%s", cmd);
        i++;
        if (i > Test->repl->N)
        {
            i = 3;
            sleep(30);
            execute_query(conn, (char*) "DISCONNECT SERVER ALL");
        }
        sleep(5);
    }
    return NULL;
}


void* transaction_thread(void* ptr)
{
    MYSQL* conn;
    int trans_result = 0;

    conn = open_conn_db_timeout(Test->repl->port[master],
                                Test->repl->IP[master],
                                (char*) "test",
                                Test->repl->user_name,
                                Test->repl->password,
                                20,
                                Test->repl->ssl);
    Test->add_result(mysql_errno(conn), "Error connecting to Binlog router, error: %s\n", mysql_error(conn));
    create_t1(conn);

    while ((exit_flag == 0) && i_trans < trans_max)
    {
        pthread_mutex_lock(&mutex);

        trans_result = transaction(conn, i_trans);
        if (trans_result != 0)
        {
            Test->tprintf("Transaction %d failed, doing master failover\n", i_trans);
            failed_transaction_num = i_trans;
            Test->tprintf("Closing connection\n");
            mysql_close(conn);
            Test->tprintf("Waiting for replication");
            sleep(15);
            Test->tprintf("Calling select_new_master()\n");
            select_new_master(Test);
            master = 2;

            conn = open_conn_db_timeout(Test->repl->port[master],
                                        Test->repl->IP[master],
                                        (char*) "test",
                                        Test->repl->user_name,
                                        Test->repl->password,
                                        20,
                                        Test->repl->ssl);
            Test->add_result(mysql_errno(conn),
                             "Error connecting to Binlog router, error: %s\n",
                             mysql_error(conn));
            Test->tprintf("Retrying transaction %d\n", i_trans);
            i_trans--;
        }
        i_trans++;
        pthread_mutex_unlock(&mutex);
    }
    i_trans--;

    exit_flag = 1;

    return NULL;
}
