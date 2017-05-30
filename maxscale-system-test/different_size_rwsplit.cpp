/**
 * @file
 * - check if Maxscale is still alive
 */

#include <my_config.h>
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
    Test->copy_all_logs(); return(Test->global_result);
}

