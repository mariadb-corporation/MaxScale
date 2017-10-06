/**
 * @file bug673.cpp regression case for bug673 ("MaxScale crashes if "Users table data" is empty and "show dbusers" is executed in maxadmin")
 *
 * - Configure wrong IP for all backends
 * - Execute maxadmin command show dbusers "RW Split Router"
 * - Check MaxScale is alive by executing maxadmin again
 * - Check that only new style object names in maxadmin commands are accepted
 */

#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.set_timeout(60);
    for (int i = 0; i < 2; i++)
    {
        char result[1024];
        test.add_result(test.maxscales->get_maxadmin_param(0, "show dbusers \"RW Split Router\"", "User names:", result) == 0,
                        "Old style objects in maxadmin commands should fail");
        test.add_result(test.maxscales->get_maxadmin_param(0, "show dbusers RW-Split-Router", "User names:", result),
                        "New style objects in maxadmin commands should succeed");
    }

    return test.global_result;
}
