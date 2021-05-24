/**
 * @file mxs922_listener_ssl.cpp MXS-922: Dynamic SSL test
 */

#include <maxtest/config_operations.hh>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    Config config(test);

    config.create_listener(Config::SERVICE_RWSPLIT);
    config.create_monitor("mysql-monitor", "mysqlmon", 500);
    config.reset();
    sleep(1);

    test->maxscales->connect_maxscale();
    test->try_query(test->maxscales->conn_rwsplit[0], "select @@server_id");
    config.create_ssl_listener(Config::SERVICE_RCONN_SLAVE);

    MYSQL* conn = open_conn(test->maxscales->readconn_master_port,
                            test->maxscales->ip4(),
                            test->maxscales->user_name,
                            test->maxscales->password,
                            true);
    test->add_result(execute_query(conn, "select @@server_id"), "SSL query to readconnroute failed");
    mysql_close(conn);

    test->maxscales->expect_running_status(true);
    int rval = test->global_result;
    delete test;
    return rval;
}
