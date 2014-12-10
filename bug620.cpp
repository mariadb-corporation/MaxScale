/**
 * @file bug620.cpp bug620 regression case ("enable_root_user=true generates errors to error log")
 *
 * - Maxscale.cnf contains RWSplit router definition with enable_root_user=true
 * - warnings are not expected in the log. All Maxscale services should be alive.
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();

    printf("Creating 'root'@'%%'\n");
    global_result += execute_query(Test->conn_rwsplit, (char *) "CREATE USER 'root'@'%'; SET PASSWORD FOR 'root'@'%' = PASSWORD('skysqlroot');");

    MYSQL * conn;

    printf("Connecting using 'root'@'%%'\n");
    conn = open_conn(Test->rwsplit_port, Test->Maxscale_IP, (char *) "root", (char *)  "skysqlroot");

    printf("Simple query...\n");
    global_result += execute_query(conn, (char *) "SELECT * from mysql.user");

    printf("Dropping 'root'@'%%'\n");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP USER 'root'@'%';");

    Test->CloseMaxscaleConn();


    global_result += CheckLogErr((char *) "Failed adding user root", FALSE);
    global_result += CheckLogErr((char *) "Error : Couldn't find suitable Master", FALSE);
    global_result += CheckMaxscaleAlive();
    return(global_result);
}
