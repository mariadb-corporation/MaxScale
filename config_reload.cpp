/**
 * @file config_reload.cpp Configuration reload test
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char str[1024];
    int global_result = 0;
    int sleep_time = 10;
    Test->read_env();
    Test->print_env();
    Test->connect_maxscale();

    if(!Test->test_maxscale_connections(true, false, false))
        global_result++;

    cout << "Changing configuration..." << endl;
    Test->reconfigure_maxscale((char *) "replication");

    if(!Test->test_maxscale_connections(true, true, true))
        global_result++;

    cout << "Changing configuration..." << endl;
    Test->reconfigure_maxscale((char *) "config_reload");

    if(!Test->test_maxscale_connections(true, false, false))
        global_result++;

    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(global_result);

}
