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
    test.check_log_err(0, " No users were loaded but 'inject_service_user' is enabled", false);
    return test.global_result;
}
