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

/*
Markus Mäkelä 2015-02-16 10:25:50 UTC
Using the following regex filter:

[MySetOptionFilter]
type=filter
module=regexfilter
options=ignorecase
match=SET OPTION SQL_QUOTE_SHOW_CREATE
replace=SET SQL_QUOTE_SHOW_CREATE

Sending the following query hangs MaxScale:

SET OPTION SQL_QUOTE_SHOW_CREATE = 1;

This happens because modutil_replace_SQL doesn't modify the SQL packet length if the resulting replacement is shorter.
Comment 1 Markus Mäkelä 2015-02-16 10:27:20 UTC
Added SQL packet length modifications to modutil_replace_SQL when the original length is different from the replacement length.
*/



#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->connect_maxscale();

    Test->tprintf("RWSplit: \n");
    fflush(stdout);
    Test->try_query(Test->conn_rwsplit, (char *) "SET OPTION SQL_QUOTE_SHOW_CREATE = 1;");
    Test->tprintf("ReadConn master: \n");
    fflush(stdout);
    Test->try_query(Test->conn_master, (char *) "SET OPTION SQL_QUOTE_SHOW_CREATE = 1;");
    Test->tprintf("readConn slave: \n");
    fflush(stdout);
    Test->try_query(Test->conn_slave, (char *) "SET OPTION SQL_QUOTE_SHOW_CREATE = 1;");

    Test->close_maxscale_connections();

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

