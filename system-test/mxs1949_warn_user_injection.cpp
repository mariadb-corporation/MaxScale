/**
 * MXS-1949: Warning for user load failure logged even when service has no users
 *
 * Check that the message is not logged when services have no servers and
 * 'inject_service_user' is enabled.
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.log_excludes(0, " No users were loaded but 'inject_service_user' is enabled");
    return test.global_result;
}
