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

    Test->set_timeout(300);
    Test->start_binlog();
    different_packet_size(Test, true);

    Test->check_maxscale_processes(1);
    Test->copy_all_logs(); return(Test->global_result);
}
