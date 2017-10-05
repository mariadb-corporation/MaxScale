/**
 * @file mxs652_bad_ssl.cpp mxs652 regression case ("ssl is configured in a wrong way, but Maxscale can be started and works")
 *
 * - Maxscale.cnf contains ssl configuration for all services in 'router' section instead of 'listener' with 'ssl=require'
 * - trying to connect to all maxscales->routers[0] without ssl and expect error
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->check_log_err((char *) "Unexpected parameter 'ssl_version'", true);


    Test->tprintf("Trying RWSplit, expecting fault\n");
    MYSQL * conn = open_conn(Test->maxscales->rwsplit_port[0], Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password,
                             false);

    if (mysql_errno(conn) == 0)
    {
        Test->add_result(1, "Configurations is wrong, but connection to RWSplit is ok\n");
        mysql_close(conn);
    }

    Test->tprintf("Trying ReadConn master, expecting fault\n");
    conn = open_conn(Test->maxscales->readconn_master_port[0], Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password,
                     false);

    if (mysql_errno(conn) == 0)
    {
        Test->add_result(1, "Configurations is wrong, but connection to ReadConn master is ok\n");
        mysql_close(conn);
    }

    Test->tprintf("Trying ReadConn slave, expecting fault\n");
    conn = open_conn(Test->maxscales->readconn_slave_port[0][0], Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password,
                     false);

    if (mysql_errno(conn) == 0)
    {
        Test->add_result(1, "Configurations is wrong, but connection to ReadConn slave is ok\n");
        mysql_close(conn);
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}

