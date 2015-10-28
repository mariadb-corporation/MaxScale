/**
 * @file bug673.cpp  regression case for bug673 ("MaxScale crashes if "Users table data" is empty and "show dbusers" is executed in maxadmin")
 *
 * - configure wrong IP for all backends
 * - execute maxadmin command show dbusers "RW Split Router"
 * - check MaxScale is alive by executing maxadmin again
 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    char result[1024];
    TestConnections * Test = new TestConnections(argc, argv);

    sleep(30);

    Test->set_timeout(20);

    Test->tprintf("Trying show dbusers \"RW Split Router\"\n");
    Test->add_result(get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dbusers \"RW Split Router\"", (char *) "No. of entries:", result), "Maxadmin failed\n");
    Test->tprintf("result %s\n", result);

    Test->tprintf("Trying show dbusers \"Read Connection Router Master\"\n");
    Test->add_result(get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dbusers \"Read Connection Router Master\"", (char *) "No. of entries:", result), "Maxadmin failed\n");
    Test->tprintf("result %s\n", result);


    Test->tprintf("Trying show dbusers \"Read Connection Router Slave\"\n");
    Test->add_result(get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dbusers \"Read Connection Router Slave\"", (char *) "No. of entries:", result), "Maxadmin failed\n");
    Test->tprintf("result %s\n", result);


    Test->tprintf("Trying again show dbusers \"RW Split Router\" to check if MaxScale is alive\n");
    Test->add_result(get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dbusers \"RW Split Router\"", (char *) "No. of entries:", result), "Maxadmin failed\n");
    Test->tprintf("result %s\n", result);

    Test->copy_all_logs(); return(Test->global_result);
}
