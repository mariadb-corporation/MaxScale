/**
 * @file mxs47.cpp Regression test for bug MXS-47 ("Session freeze when small tail packet")
 * - execute SELECT REPEAT('a',i), where 'i' is changing from 1 to 3000 with stride of 7 using readwritesplit
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    int iterations = 5000;

    test.tprintf("Executing `SELECT REPEAT('a', X );` for X = 0..%d with a stride of 7", iterations);
    test.connect_maxscale();

    for (int i = 1; i < iterations; i += 7)
    {
        char str[1024];
        sprintf(str, "SELECT REPEAT('a',%d)", i);

        test.set_timeout(15);
        test.try_query(test.conn_rwsplit, str);
    }

    test.close_maxscale_connections();

    return test.global_result;
}
