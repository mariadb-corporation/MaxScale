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
    Test->set_timeout(300);
    char str[1024];

    int sleep_time = 10;

    Test->connect_maxscale();

    if(!Test->test_maxscale_connections(true, false, false))
        Test->add_result(1, "failed");

    cout << "Changing configuration..." << endl;
    Test->reconfigure_maxscale((char *) "replication");

    if(!Test->test_maxscale_connections(true, true, true))
        Test->add_result(1, "failed");

    cout << "Changing configuration..." << endl;
    Test->reconfigure_maxscale((char *) "config_reload");

    if(!Test->test_maxscale_connections(true, false, false))
        Test->add_result(1, "failed");

    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(Test->global_result);

}
