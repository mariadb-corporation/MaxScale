/**
 * @file gui_demo.cpp Dummy test to start Maxscale GUI demo
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    return test.global_result;
}
