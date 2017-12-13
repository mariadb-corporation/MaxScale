/**
 * Test auto_failover
 */

#include "testconnections.h"
#include "failover_common.cpp"

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    TestConnections test(argc, argv);

    // Wait a few seconds
    sleep(5);
    basic_test(test);

    // Test 1
    int node0_id = prepare_test_1(test);
    sleep(10);
    check_test_1(test, node0_id);

    // Test 2
    prepare_test_2(test);
    sleep(10);
    check_test_2(test);

    // Test 3
    prepare_test_3(test);
    sleep(10);
    check_test_3(test);

    test.repl->fix_replication();
    return test.global_result;
}
