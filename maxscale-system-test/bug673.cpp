/**
 * @file bug673.cpp regression case for bug673 ("MaxScale crashes if "Users table data" is empty and "show dbusers" is executed in maxadmin")
 *
 * - configure wrong IP for all backends
 * - execute maxadmin command show dbusers "RW Split Router"
 * - check MaxScale is alive by executing maxadmin again
 */

#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    char result[1024];
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(20);

    for (int i = 0; i < 2; i++)
    {
        Test->tprintf("Trying show dbusers \"RW Split Router\"\n");
        Test->add_result(Test->get_maxadmin_param((char *) "show dbusers \"RW Split Router\"", (char *) "User names:",
                         result), "Maxadmin failed\n");
        Test->tprintf("result %s\n", result);
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
