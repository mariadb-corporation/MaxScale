/**
 * @file mxs127.cpp - bug mxs-127 regression case ("disable_sescmd_history causes MaxScale to crash under load")
 * - execute set @test=%d 10000 times against RWSplit, ReadConn Master and ReadConn Slave
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    int i;
    char sql[256];

    Test->connect_maxscale();

    Test->tprintf("RWSplit: Executing set @test=i 10000 times\n");
    for (i = 0; i < 10000; i++)
    {
        Test->set_timeout(5);
        sprintf(sql, "set @test=%d", i);
        Test->try_query(Test->maxscales->conn_rwsplit[0], sql);
    }
    Test->tprintf("done!\n");

    printf("ReadConn Master: Executing set @test=i 10000 times\n");
    for (i = 0; i < 10000; i++)
    {
        Test->set_timeout(5);
        sprintf(sql, "set @test=%d", i);
        Test->try_query(Test->maxscales->conn_master[0], sql);
    }
    Test->tprintf("done!\n");

    Test->tprintf("ReadConn Slave: Executing set @test=i 10000 times\n");
    for (i = 0; i < 10000; i++)
    {
        Test->set_timeout(5);
        sprintf(sql, "set @test=%d", i);
        Test->try_query(Test->maxscales->conn_slave[0], sql);
    }
    Test->tprintf("done!\n");

    Test->close_maxscale_connections();
    int rval = Test->global_result;
    delete Test;
    return rval;
}

