/**
 * @file gui_demo.cpp Dummy test to start Maxscale GUI demo
 */


#include <iostream>
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->start_maxscale(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
