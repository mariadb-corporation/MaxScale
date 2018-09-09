/**
 * @file mxs710_bad_socket.cpp mxs710_bad_socket regression case (Maxscale does not startup properly and
 *crashes after trying to login to database)
 * - try to start maxscale with "socket=/var/lib/mysqld/mysql.sock" in the listener definition
 * - do not expect crash
 * - try the same with two listers for one service, one of them uses unreachable port
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->check_maxscale_processes(0);
    Test->check_log_err(0, "Fatal", false);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
