/**
 * MXS-1929: Runtime filter creation
 */

#include "testconnections.h"
#include <iostream>

using namespace std;

void create_all(TestConnections& test)
{
    test.check_maxctrl("create server server1 " + string(test.repl->ip(0)) + " " + to_string(test.repl->port[0]));
    test.check_maxctrl("create server server2 " + string(test.repl->ip(1)) + " " + to_string(test.repl->port[1]));
    test.check_maxctrl("create server server3 " + string(test.repl->ip(2)) + " " + to_string(test.repl->port[2]));
    test.check_maxctrl("create service svc1 readwritesplit user=skysql password=skysql --servers server1 server2 server3");
    test.check_maxctrl("create listener svc1 listener1 4006");
    test.check_maxctrl("create monitor mon1 mariadbmon --monitor-user skysql --monitor-password skysql --servers server1 server2 server3");
}

void destroy_all(TestConnections& test)
{
    test.check_maxctrl("unlink monitor mon1 server1 server2 server3");
    test.check_maxctrl("unlink service svc1 server1 server2 server3");
    test.check_maxctrl("destroy listener svc1 listener1");
    test.check_maxctrl("destroy service svc1");
    test.check_maxctrl("destroy monitor mon1");
    test.check_maxctrl("destroy server server1");
    test.check_maxctrl("destroy server server2");
    test.check_maxctrl("destroy server server3");
}

void basic(TestConnections& test)
{
    test.check_maxctrl("create filter test1 regexfilter \"match=SELECT 1\" \"replace=SELECT 2\"");
    test.check_maxctrl("alter service filters svc1 test1");

    Connection c = test.maxscales->rwsplit();
    c.connect();
    test.assert(c.check("SELECT 1", "2"), "The regex filter did not replace the query");


    auto res = test.maxctrl("destroy filter test1");
    test.assert(res.first != 0, "Destruction should fail when filter is in use");

    test.check_maxctrl("alter service filters svc1");
    test.check_maxctrl("destroy filter test1");

    test.assert(c.check("SELECT 1", "2"), "The filter should not yet be destroyed");

    c.disconnect();
    c.connect();

    test.assert(c.check("SELECT 1", "1"), "The filter should be destroyed");
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.tprintf("Creating servers, monitors and services");
    create_all(test);

    test.tprintf("Basic test");
    basic(test);

    test.tprintf("Destroying servers, monitors and services");
    destroy_all(test);

    return test.global_result;
}
