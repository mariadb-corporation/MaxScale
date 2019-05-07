/**
 * @file clustrix_mon.cpp - simple Clustrix monitor test
 * Just creates Clustrix cluster and connect Maxscale to it
 * It can be used as a template for clustrix tests
 *
 * See Clustrix_nodes.h for details about configiration
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    int i;
    TestConnections* Test = new TestConnections(argc, argv);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
