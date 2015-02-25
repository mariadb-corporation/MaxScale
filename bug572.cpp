/**
 * @file bug572.cpp  regression case for bug 572 ( " If reading a user from users table fails, MaxScale fails" )
 *
 * - try GRANT with wrong IP using all Maxscale services:
 *  + GRANT ALL PRIVILEGES ON *.* TO  'foo'@'*.foo.notexists' IDENTIFIED BY 'foo';
 *  + GRANT ALL PRIVILEGES ON *.* TO  'bar'@'127.0.0.*' IDENTIFIED BY 'bar'
 *  + DROP USER 'foo'@'*.foo.notexists'
 *  + DROP USER 'bar'@'127.0.0.*'
 * - do "select * from mysql.user" using RWSplit to check if Maxsclae crashed
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int CreateDropBadUser(MYSQL * conn)
{
    int global_result = 0;
    global_result += execute_query(conn, (char *) "GRANT ALL PRIVILEGES ON *.* TO  'foo'@'*.foo.notexists' IDENTIFIED BY 'foo';");
    global_result += execute_query(conn, (char *) "GRANT ALL PRIVILEGES ON *.* TO  'bar'@'127.0.0.*' IDENTIFIED BY 'bar'");
    global_result += execute_query(conn, (char *) "DROP USER 'foo'@'*.foo.notexists'");
    global_result += execute_query(conn, (char *) "DROP USER 'bar'@'127.0.0.*'");
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    printf("Trying GRANT for with bad IP: RWSplit\n"); fflush(stdout);
    global_result += CreateDropBadUser(Test->conn_rwsplit); fflush(stdout);
    printf("Trying GRANT for with bad IP: ReadConn slave\n"); fflush(stdout);
    global_result += CreateDropBadUser(Test->conn_slave);
    printf("Trying GRANT for with bad IP: ReadConn master\n"); fflush(stdout);
    global_result += CreateDropBadUser(Test->conn_master);
    printf("Trying SELECT to check if Maxscale hangs\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "select * from mysql.user");

    Test->Copy_all_logs(); return(global_result);
}
