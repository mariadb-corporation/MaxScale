/**
 * @file mxs813_long_hostname - regression case for crash if long host name is used for binlog router
 * - configure binlog router setup
 * - stop slave
 * - change master to master_host=<very_long_hostname>
 * - start slave
 * - show slave status
 * - show slave status;
 * - show slave status\G
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->start_binlog(0);

    MYSQL * binlog = open_conn_no_db(Test->maxscales->binlog_port[0], Test->maxscales->IP[0],
                                     Test->repl->user_name,
                                     Test->repl->password, Test->ssl);

    Test->tprintf("stop slave\n");
    Test->try_query(binlog, "stop slave");
    Test->tprintf("change master to..\n");
    Test->try_query(binlog,
                    "change master to master_host='12345678901234567890123456789012345678901234567890123456789012345678900000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000.com';");
    Test->tprintf("start slave\n");
    Test->try_query(binlog, "start slave");
    Test->tprintf("show slave status\n");
    Test->try_query(binlog, "show slave status");
    Test->tprintf("show slave status error: %s\n", mysql_error(binlog));
    execute_query(binlog, "show slave status;");
    execute_query(binlog, "show slave status\\G");

    mysql_close(binlog);

    Test->check_maxscale_processes(0, 1);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
