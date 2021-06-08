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


#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>
#include <string>

typedef struct
{
    int         port;
    std::string ip;
    std::string user;
    std::string password;
    bool        ssl;
    int         exit_flag;
} openclose_thread_data;

void* disconnect_thread(void* ptr);

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.maxscale->ssh_node_f(true,
                              "sysctl net.ipv4.tcp_tw_reuse=1 net.ipv4.tcp_tw_recycle=1 "
                              "net.core.somaxconn=10000 net.ipv4.tcp_max_syn_backlog=10000");

    test.reset_timeout();
    test.maxscale->connect_maxscale();
    create_t1(test.maxscale->conn_rwsplit[0]);
    execute_query(test.maxscale->conn_rwsplit[0], "set global max_connections=1000");
    test.maxscale->close_maxscale_connections();

    test.tprintf("Create query load");
    int load_threads_num = 10;
    openclose_thread_data data_master[load_threads_num];
    pthread_t thread_master[load_threads_num];

    /* Create independent threads each of them will create some load on Master */
    for (int i = 0; i < load_threads_num; i++)
    {
        data_master[i].exit_flag = 0;
        data_master[i].ip = test.maxscale->ip4();
        data_master[i].port = test.maxscale->rwsplit_port;
        data_master[i].user = test.maxscale->user_name;
        data_master[i].password = test.maxscale->password;
        data_master[i].ssl = test.maxscale_ssl;
        pthread_create(&thread_master[i], NULL, disconnect_thread, &data_master[i]);
    }

    int iterations = 5;
    int sleep_interval = 10;

    for (int i = 0; i < iterations; i++)
    {
        sleep(sleep_interval);

        test.reset_timeout();
        test.tprintf("Block master");
        test.repl->block_node(0);

        sleep(sleep_interval);

        test.reset_timeout();
        test.tprintf("Unblock master");
        test.repl->unblock_node(0);
    }

    test.tprintf("Waiting for all master load threads exit");
    for (int i = 0; i < load_threads_num; i++)
    {
        test.reset_timeout();
        data_master[i].exit_flag = 1;
        pthread_join(thread_master[i], NULL);
    }

    test.tprintf("Check that replication works");
    sleep(1);
    auto& mxs = test.maxscale->maxscale_b();
    mxs.check_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st});
    if (!test.ok())
    {
        return test.global_result;
    }

    // Try to connect over a period of 60 seconds. It is possible that
    // there are no available network sockets which means we'll have to
    // wait until some of them become available. This is caused by how the
    // TCP stack works.
    for (int i = 0; i < 60; i++)
    {
        test.reset_timeout();
        test.set_verbose(true);
        int rc = test.maxscale->connect_maxscale();
        test.set_verbose(false);

        if (rc == 0)
        {
            break;
        }
        sleep(1);
    }

    test.try_query(test.maxscale->conn_rwsplit[0], "DROP TABLE IF EXISTS t1");
    test.maxscale->close_maxscale_connections();

    test.maxscale->wait_for_monitor();
    test.check_maxscale_alive();
    test.log_excludes("due to authentication failure");
    test.log_excludes("due to handshake failure");
    test.log_excludes("Refresh rate limit exceeded for load of users' table");

    return test.global_result;
}


void* disconnect_thread(void* ptr)
{
    openclose_thread_data* data = (openclose_thread_data*) ptr;
    char sql[1000000];

    sleep(3);
    create_insert_string(sql, 50000, 2);

    while (data->exit_flag == 0)
    {
        MYSQL* conn = open_conn_db_timeout(data->port,
                                           data->ip,
                                           "test",
                                           data->user,
                                           data->password,
                                           10,
                                           data->ssl);
        execute_query_silent(conn, sql);
        mysql_close(conn);
    }

    return NULL;
}
