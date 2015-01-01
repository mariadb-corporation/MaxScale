/**
 * @file bug676.cpp  reproducing attempt ("Memory corruption when users with long hostnames that can no the resolved are loaded into MaxScale")
 *
 *
 * - check MaxScale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "mariadb_func.h"

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    //Test->ConnectMaxscale();


    MYSQL * conn = open_conn_no_db(Test->rwsplit_port, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

    printf("selecting DB 'test' for rwsplit\n"); fflush(stdout);
    global_result += execute_query(conn, "USE test");

    //Test->CloseMaxscaleConn();
    printf("Closing connection\n"); fflush(stdout);
    mysql_close(conn);

    global_result += CheckMaxscaleAlive();

    return(global_result);
}

