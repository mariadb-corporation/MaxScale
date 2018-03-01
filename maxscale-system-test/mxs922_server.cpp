/**
 * @file mxs922_server.cpp MXS-922: Server creation test
 *
 */

#include "testconnections.h"
#include "config_operations.h"

int check_server_id(TestConnections *test, int idx)
{
    test->close_maxscale_connections();
    test->connect_maxscale();

    int a = test->repl->get_server_id(idx);
    int b = -1;
    char str[1024];

    if (find_field(test->conn_rwsplit, "SELECT @@server_id", "@@server_id", str) == 0)
    {
        b = atoi(str);
    }

    return a - b;
}

int main(int argc, char *argv[])
{
    TestConnections *test = new TestConnections(argc, argv);
    Config config(test);

    config.create_all_listeners();
    config.create_monitor("mysql-monitor", "mysqlmon", 500);

    test->tprintf("Testing server creation and destruction");

    config.create_server(1);
    config.create_server(1);
    config.check_server_count(1);
    config.destroy_server(1);
    config.destroy_server(1);
    config.check_server_count(0);
    test->check_maxscale_processes(1);

    test->tprintf("Testing adding of server to service");

    config.create_server(1);
    config.add_server(1);
    config.check_server_count(1);
    sleep(1);
    test->check_maxscale_alive();
    config.remove_server(1);
    config.destroy_server(1);
    config.check_server_count(0);

    test->tprintf("Testing altering of server");

    config.create_server(1);
    config.add_server(1);
    config.alter_server(1, "address", test->repl->IP[1]);
    sleep(1);
    test->check_maxscale_alive();
    config.alter_server(1, "address", "This-is-not-the-address-you-are-looking-for");
    config.alter_server(1, "port", 12345);
    test->connect_maxscale();
    test->add_result(execute_query_silent(test->conn_rwsplit, "SELECT 1") == 0,
                     "Query with bad address should fail");

    config.remove_server(1);
    config.destroy_server(1);


    test->tprintf("Testing server weights");

    config.reset();
    sleep(1);
    test->repl->connect();

    config.alter_server(1, "weight", 1);
    config.alter_server(2, "weight", 1);
    config.alter_server(3, "weight", 1000);
    test->add_result(check_server_id(test, 3), "The server_id values don't match");

    config.alter_server(1, "weight", 1);
    config.alter_server(2, "weight", 1000);
    config.alter_server(3, "weight", 1);
    test->add_result(check_server_id(test, 2), "The server_id values don't match");

    config.alter_server(1, "weight", 1000);
    config.alter_server(2, "weight", 1);
    config.alter_server(3, "weight", 1);
    test->add_result(check_server_id(test, 1), "The server_id values don't match");

    config.reset();
    sleep(1);
    test->check_maxscale_alive();
    int rval = test->global_result;
    delete test;
    return rval;
}
