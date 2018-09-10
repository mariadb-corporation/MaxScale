/**
 * @file mxs781_binlog_wrong_passwrd.cpp Try to configure binlog router to use wrong password for Master and
 * check 'slave status' on binlog
 * - try to put wrong password when connect binlog router to real master
 * - check binlog router status using 'show slave status', expect 'Slave stopped'
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"

const char* setup_binlog_wrong_passwrd
    =
        "change master to MASTER_HOST='%s',\
         MASTER_USER='repl',\
         MASTER_PASSWORD='wrong_password',\
         MASTER_LOG_FILE='mar-bin.000001',\
         MASTER_LOG_POS=4,\
         MASTER_PORT=%d";


int main(int argc, char* argv[])
{

    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    char str[1024];

    Test->tprintf("Connecting to all backend nodes\n");
    Test->add_result(Test->repl->connect(), "Connecting to backed failed\n");

    Test->prepare_binlog(0);

    Test->tprintf("Connecting to MaxScale binlog router (with any DB)\n");
    Test->set_timeout(30);
    MYSQL* binlog = open_conn_no_db(Test->maxscales->binlog_port[0],
                                    Test->maxscales->IP[0],
                                    Test->repl->user_name,
                                    Test->repl->password,
                                    Test->ssl);

    Test->add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    Test->tprintf("'Stop slave' to binlog\n");
    Test->set_timeout(10);
    execute_query(binlog, (char*) "stop slave");

    Test->tprintf("configuring Maxscale binlog router with wrong password\n");
    sprintf(str, setup_binlog_wrong_passwrd, Test->repl->IP[0], Test->repl->port[0]);
    Test->tprintf("binlog setup sql: %s\n", str);
    Test->set_timeout(10);
    execute_query(binlog, "%s", str);
    Test->tprintf("Error: %s\n", mysql_error(binlog));

    Test->tprintf("'start slave' to binlog\n");
    Test->set_timeout(10);
    execute_query(binlog, "start slave");
    Test->tprintf("Error: %s\n", mysql_error(binlog));

    Test->stop_timeout();
    sleep(25);
    Test->set_timeout(10);
    find_field(binlog, (char*) "show slave status", (char*) "Slave_IO_State", str);
    Test->add_result(strcasecmp(str, "Slave stopped"), "Wrong slave state: %s\n", str);

    Test->set_timeout(10);
    find_field(binlog, (char*) "show slave status", (char*) "Last_Error", str);
    Test->add_result(strcasecmp(str, "#28000 Authentication with master server failed"),
                     "Wrong slave state: %s\n",
                     str);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
