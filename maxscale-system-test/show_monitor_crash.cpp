/**
 * @file show_monitor_crash.cpp show_monitor_crash regression case for crash if maxadmin 'show monitors' command is issued, but no monitor is not running
 *
 * - maxscale.cnf contains wrong monitor config (user name is wrong)
 * - issue 'show monitors' maxadmin command
 * - check for crash
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(100);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show monitors");
    sleep(5);
    Test->check_log_err(0, (char *) "Failed to start monitor", true);
    Test->check_log_err(0, (char *) "fatal signal 11", false);

    Test->check_maxscale_processes(0, 1);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
