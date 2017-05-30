/**
 * @file bug730.cpp regression case for bug 730 ("Regex filter and shorter than original replacement queries MaxScale")
 *
 * - setup regex filter, add it to all routers
 * @verbatim
[MySetOptionFilter]
type=filter
module=regexfilter
options=ignorecase
match=SET OPTION SQL_QUOTE_SHOW_CREATE
replace=SET SQL_QUOTE_SHOW_CREATE

 @endverbatim
 * - try SET OPTION SQL_QUOTE_SHOW_CREATE = 1; against all routers
 * - check if Maxscale alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->connect_maxscale();

    Test->tprintf("RWSplit: \n"); fflush(stdout);
    Test->try_query(Test->conn_rwsplit, (char *) "SET OPTION SQL_QUOTE_SHOW_CREATE = 1;");
    Test->tprintf("ReadConn master: \n"); fflush(stdout);
    Test->try_query(Test->conn_master, (char *) "SET OPTION SQL_QUOTE_SHOW_CREATE = 1;");
    Test->tprintf("readConn slave: \n"); fflush(stdout);
    Test->try_query(Test->conn_slave, (char *) "SET OPTION SQL_QUOTE_SHOW_CREATE = 1;");

    Test->close_maxscale_connections();

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}

