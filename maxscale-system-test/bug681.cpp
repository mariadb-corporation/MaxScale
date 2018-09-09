/**
 * @file bug681.cpp  - regression test for bug681 ("crash if max_slave_connections=10% and 4 or less backends
 *are configured")
 *
 * - Configure RWSplit with max_slave_connections=10%
 * - check ReadConn master and ReadConn slave are alive and RWSplit is not started
 */

/*
 *  Timofey Turenko 2015-01-05 11:33:29 UTC
 *  try to start MaxScale with max_slave_connections=10%
 *
 *
 *  Result:
 *  Program terminated with signal 8, Arithmetic exception.
 #0  0x00007ff0517fee3f in have_enough_servers (p_rses=0x7fff9ed17ed0, min_nsrv=1, router_nsrv=3,
 * router=0x397c2b0)
 *   at /usr/local/skysql/maxscale/server/modules/routing/readwritesplit/readwritesplit.c:4668
 *  4668                                    LOGIF(LE, (skygw_log_write_flush(
 *  Comment 1 Markus Mäkelä 2015-01-05 11:59:38 UTC
 *  Added casts to floating point values when doing divisions.
 *
 */


#include <iostream>
#include "testconnections.h"
#include "mariadb_func.h"

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    Test->maxscales->connect_maxscale(0);

    if (mysql_errno(Test->maxscales->conn_rwsplit[0]) == 0)
    {
        Test->add_result(1, "RWSplit services should fail, but it is started\n");
    }

    Test->tprintf("Trying query to ReadConn master\n");
    Test->try_query(Test->maxscales->conn_master[0], "show processlist;");
    Test->tprintf("Trying query to ReadConn slave\n");
    Test->try_query(Test->maxscales->conn_slave[0], "show processlist;");

    Test->maxscales->close_maxscale_connections(0);

    Test->check_log_err(0, "There are too few backend servers configured in", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
