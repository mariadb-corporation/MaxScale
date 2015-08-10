/**
 * @file mxs47.cpp Regression test for bug MXS-47 ("Session freeze when small tail packet")
 * - execute SELECT REPEAT('a',i), where 'i' is changing from 1 to 50000 using all Maxscale services
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char str[1024];
    int global_result = 0;
    int sleep_time = 10;
    Test->read_env();
    Test->print_env();
    Test->connect_maxscale();

    cout << "Testing RWSplit, expecting success" << endl;
    if((global_result += execute_query(Test->conn_rwsplit, "select 1")))
        cout << "Error: Query failed" << endl;
    cout << "Testing ReadConnRoute Master, expecting failure" << endl;
    if(execute_query(Test->conn_master, "select 1") == 0)
    {
        cout << "Error: Query succeeded" << endl;
        global_result++;
    }
    cout << "Testing ReadConnRoute Slave, expecting failure" << endl;
    if(execute_query(Test->conn_slave, "select 1") == 0)
    {
        cout << "Error: Query succeeded" << endl;
        global_result++;
    }

    cout << "Reloading configuration via SIGHUP" << endl;
    if(Test->execute_ssh_maxscale((char*)"sed -i -e 's/#//g' /etc/maxscale.cnf"))
    {
        cout << "SSH command failed!" << endl;
        global_result++;
    }

    if(Test->execute_ssh_maxscale((char*)"killall -HUP maxscale"))
    {
        cout << "SSH command failed!" << endl;
        global_result++;
    }
    cout << "Sleeping for " << sleep_time <<" seconds" << endl;
    sleep(sleep_time);
    Test->close_maxscale_connections();
    Test->connect_maxscale();

    cout << "Testing RWSplit, expecting success" << endl;
    if((global_result += execute_query(Test->conn_rwsplit, "select 1")))
        cout << "Error: Query failed" << endl;
    cout << "Testing ReadConnRoute Master, expecting success" << endl;
    if((global_result += execute_query(Test->conn_master, "select 1")))
        cout << "Error: Query failed" << endl;
    cout << "Testing ReadConnRoute Slave, expecting success" << endl;
    if((global_result += execute_query(Test->conn_slave, "select 1")))
        cout << "Error: Query failed" << endl;

    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(global_result);

}
