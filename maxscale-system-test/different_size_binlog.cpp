/**
 * @file different_size_binlog.cpp Tries INSERTs with size close to 0x0ffffff * N
 * - configure binlog
 * - executes inserts with size: from 0x0ffffff * N - X up to 0x0ffffff * N - X
 * (N = 3, X = 50 or 20 for 'soke' test)
 * - check if Maxscale is still alive
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "different_size.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(300);
    Test->start_binlog(0);
    different_packet_size(Test, true);

    Test->check_maxscale_processes(0, 1);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
