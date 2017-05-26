/**
 * @file mxs922_listener_ssl.cpp MXS-922: Dynamic SSL test
 */

#include "testconnections.h"
#include "config_operations.h"

int main(int argc, char *argv[])
{
    TestConnections *test = new TestConnections(argc, argv);
    Config config(test);

    config.create_listener(Config::SERVICE_RWSPLIT);
    config.create_monitor("mysql-monitor", "mysqlmon", 500);
    config.reset();
    sleep(1);

    test->connect_maxscale();
    test->try_query(test->conn_rwsplit, "select @@server_id") == 0;
    config.create_ssl_listener(Config::SERVICE_RCONN_SLAVE);

    MYSQL *conn = open_conn(test->readconn_master_port, test->maxscale_IP, test->maxscale_user,
                            test->maxscale_password, true);
    test->add_result(execute_query(conn, "select @@server_id"), "SSL query failed");
    mysql_close(conn);

    test->check_maxscale_processes(1);
    int rval = test->global_result;
    delete test;
    return rval;
}
