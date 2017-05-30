/**
 * @file bug711.cpp bug711 regression case (Some MySQL Workbench Management actions hang with R/W split router)
 * - configure all routers with use_sql_variables_in=all
 * - try SHOW GLOBAL STATUS with all routers
 * - check if Maxscale is still alive
 */

#include <my_config.h>
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
    Test->copy_all_logs(); return(Test->global_result);
}
