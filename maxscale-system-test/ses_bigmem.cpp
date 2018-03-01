/**
 * @file ses_bigmem Executes a lot of session commands with "disable_sescmd_history=true" and check that memory consumption is not increasing
 * (relates to MXS-672 "maxscale possible memory leak"
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    unsigned long maxscale_mem;

    Test->set_timeout(10);

    Test->connect_maxscale();
    int iterations = Test->smoke ? 100000 : 1000000;
    int r = Test->smoke ? 1 : 3;

    for (int j = 0; j < r; j++)
    {
        for (int i = 0; i < iterations; i++)
        {
            Test->set_timeout(10);
            Test->try_query(Test->routers[j], (char*) "set autocommit=0;");
            Test->try_query(Test->routers[j], (char*) "select 1;");
            Test->try_query(Test->routers[j], (char*) "set autocommit=1;");
            Test->try_query(Test->routers[j], (char*) "select 2;");
            if ((i / 1000) * 1000 == i)
            {
                Test->tprintf("i=%d\n", i);
            }
        }

        maxscale_mem = Test->get_maxscale_memsize();
        Test->tprintf("Maxscale process uses %lu KBytes\n", maxscale_mem);

        if (maxscale_mem > 2000000)
        {
            Test->add_result(1, "Maxscale consumes too much memory\n");
        }
    }

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}

