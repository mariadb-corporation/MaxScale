/**
 * @file bug673.cpp regression case for bug673 ("MaxScale crashes if "Users table data" is empty and "show
 * dbusers" is executed in maxadmin")
 *
 * - Configure wrong IP for all backends
 * - Execute maxadmin command show dbusers "RW Split Router"
 * - Check MaxScale is alive by executing maxadmin again
 * - Check that only new style object names in maxadmin commands are accepted
 */

#include <maxtest/testconnections.h>
#include <maxtest/maxadmin_operations.h>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.set_timeout(60);
    for (int i = 0; i < 2; i++)
    {
        constexpr const char* old_cmd = "maxadmin show dbusers \"RW Split Router\"|grep 'User names'";
        constexpr const char* new_cmd = "maxadmin show dbusers RW-Split-Router|grep 'User names'";
        test.expect(test.maxscales->ssh_node_f(0, true, old_cmd) != 0,
                    "Old style objects in maxadmin commands should fail");
        test.expect(test.maxscales->ssh_node_f(0, true, new_cmd) == 0,
                    "New style objects in maxadmin commands should succeed");
    }

    return test.global_result;
}
