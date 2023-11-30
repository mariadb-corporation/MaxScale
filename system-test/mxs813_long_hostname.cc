/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

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
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);

    Test->start_binlog(0);

    MYSQL* binlog = open_conn_no_db(Test->maxscales->binlog_port[0],
                                    Test->maxscales->ip4(0),
                                    Test->repl->user_name,
                                    Test->repl->password,
                                    Test->ssl);

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
