/**
 * @file mxs127.cpp - bug mxs-127 regression case ("disable_sescmd_history causes MaxScale to crash under load")
 * - execute et @test=%d 10000 times against RWSplit, ReadConn Master and ReadConn Slave
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;
    char sql[256];

    Test->read_env();
    Test->print_env();

    Test->connect_maxscale();

    printf("RWSplit: Executing set @test=i 10000 times\n");  fflush(stdout);
    for (i = 0; i < 10000; i++) {
        sprintf(sql, "set @test=%d", i);
        global_result += execute_query(Test->conn_rwsplit, sql);
    }
    printf("done!\n");

    printf("ReadConn Master: Executing set @test=i 10000 times\n");  fflush(stdout);
    for (i = 0; i < 10000; i++) {
        sprintf(sql, "set @test=%d", i);
        global_result += execute_query(Test->conn_master, sql);
    }
    printf("done!\n");

    printf("ReadConn Slave: Executing set @test=i 10000 times\n");  fflush(stdout);
    for (i = 0; i < 10000; i++) {
        sprintf(sql, "set @test=%d", i);
        global_result += execute_query(Test->conn_slave, sql);
    }
    printf("done!\n");

    Test->close_maxscale_connections();
    Test->copy_all_logs(); return(global_result);
}

