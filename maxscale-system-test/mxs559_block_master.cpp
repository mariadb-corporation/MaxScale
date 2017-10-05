
/**
 * @file mxs559_block_master Playing with blocking and unblocking Master
 * It does not reproduce the bug in reliavle way, but it is a good
 * load and robustness test
 * - create load on Master RWSplit
 * - block and unblock Master in the loop
 * - repeat with different time between block/unblock
 * - check logs for lack of errors "authentication failure", "handshake failure"
 * - check for lack of crashes in the log
 */


#include "testconnections.h"
#include "sql_t1.h"
#include <string>

typedef struct
{
    int port;
    std::string ip;
    std::string user;
    std::string password;
    bool ssl;
    int exit_flag;
} openclose_thread_data;

void *disconnect_thread(void *ptr);

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    test.ssh_maxscale(true, "sysctl net.ipv4.tcp_tw_reuse=1 net.ipv4.tcp_tw_recycle=1 "
                       "net.core.somaxconn=10000 net.ipv4.tcp_max_syn_backlog=10000");

    test.set_timeout(60);
    test.connect_maxscale();
    create_t1(test.maxscales->conn_rwsplit[0]);
    execute_query(test.maxscales->conn_rwsplit[0], "set global max_connections=1000");
    test.close_maxscale_connections();

    test.tprintf("Create query load");
    int load_threads_num = 10;
    openclose_thread_data data_master[load_threads_num];
    pthread_t thread_master[load_threads_num];

    /* Create independent threads each of them will create some load on Master */
    for (int i = 0; i < load_threads_num; i++)
    {
        data_master[i].exit_flag = 0;
        data_master[i].ip = test.maxscales->IP[0];
        data_master[i].port = test.maxscales->rwsplit_port[0];
        data_master[i].user = test.maxscales->user_name;
        data_master[i].password = test.maxscales->password;
        data_master[i].ssl = test.ssl;
        pthread_create(&thread_master[i], NULL, disconnect_thread, &data_master[i]);
    }

    int iterations = 5;
    int sleep_interval = 10;

    for (int i = 0; i < iterations; i++)
    {
        test.stop_timeout();
        sleep(sleep_interval);

        test.set_timeout(60);
        test.tprintf("Block master");
        test.repl->block_node(0);

        test.stop_timeout();
        sleep(sleep_interval);

        test.set_timeout(60);
        test.tprintf("Unblock master");
        test.repl->unblock_node(0);
    }

    test.tprintf("Waiting for all master load threads exit");
    for (int i = 0; i < load_threads_num; i++)
    {
        test.set_timeout(240);
        data_master[i].exit_flag = 1;
        pthread_join(thread_master[i], NULL);
    }

    test.stop_timeout();
    test.tprintf("Make sure that replication works");
    test.repl->flush_hosts();
    if (!test.repl->fix_replication())
    {
        test.tprintf("Replication is broken!");
    }

    // Try to connect over a period of 60 seconds. It is possible that
    // there are no available network sockets which means we'll have to
    // wait until some of them become available. This is caused by how the
    // TCP stack works.
    for (int i = 0; i < 60; i++)
    {
        test.set_timeout(60);
        test.verbose = true;
        int rc = test.connect_maxscale();
        test.verbose = false;

        if (rc == 0)
        {
            break;
        }
        sleep(1);
    }

    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS t1");
    test.close_maxscale_connections();

    test.check_maxscale_alive();
    test.check_log_err("due to authentication failure", false);
    test.check_log_err("fatal signal 11", false);
    test.check_log_err("due to handshake failure", false);
    test.check_log_err("Refresh rate limit exceeded for load of users' table", false);

    return test.global_result;
}


void *disconnect_thread( void *ptr )
{
    openclose_thread_data *data = (openclose_thread_data*) ptr;
    char sql[1000000];

    sleep(3);
    create_insert_string(sql, 50000, 2);

    while (data->exit_flag == 0)
    {
        MYSQL *conn = open_conn_db_timeout(data->port, data->ip, "test",
                                           data->user, data->password,
                                           10, data->ssl);
        execute_query_silent(conn, sql);
        mysql_close(conn);
    }

    return NULL;
}

