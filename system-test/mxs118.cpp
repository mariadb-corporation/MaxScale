/**
 * @file mxs118.cpp bug mxs118 regression case ("Two monitors loaded at the same time result into not working
 * installation")
 *
 * - Configure two monitors using same backend serves
 * - try to connect to maxscale
 * - check logs for warning
 */

#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.maxscale->restart_maxscale();

    test.log_includes("is already monitored by");

    return test.global_result;
}
