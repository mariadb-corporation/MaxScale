/**
 * @file bug711.cpp bug711 regression case (Some MySQL Workbench Management actions hang with R/W split router)
 * - configure rwsplit with use_sql_variables_in=all
 * - try SHOW GLOBAL STATUS with all routers
 * - check if Maxscale is still alive
 */

/*
Massimiliano 2015-01-29 15:35:52 UTC
Some MySQL Workbench Management actions hang with R/W split router

MySQL Workbench 6.2 on OSX


When selecting "Users and Privileges" the client gets hanged.



The quick solution is setting "use_sql_variables_in" in MaxScale config file for R/W split router section

[RW_Split]
type=service
router=readwritesplit
servers=server3,server2
user=massi
passwd=xxxx

use_sql_variables_in=master



This way everything seems ok

*/


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->connect_maxscale();
    Test->set_timeout(10);
    Test->tprintf("Trying SHOW GLOBAL STATUS against RWSplit\n");
    Test->try_query(Test->conn_rwsplit, (char *) "SHOW GLOBAL STATUS;");
    Test->tprintf("Trying SHOW GLOBAL STATUS against ReadConn master\n");
    Test->try_query(Test->conn_master,  (char *) "SHOW GLOBAL STATUS;");
    Test->tprintf("Trying SHOW GLOBAL STATUS against ReadConn slave\n");
    Test->try_query(Test->conn_slave,   (char *) "SHOW GLOBAL STATUS;");
    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
