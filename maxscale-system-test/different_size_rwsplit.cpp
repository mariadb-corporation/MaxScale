/**
 * @file different_size_rwsplit.cpp Tries INSERTs with size close to 0x0ffffff * N
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

    different_packet_size(Test, false);

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}

