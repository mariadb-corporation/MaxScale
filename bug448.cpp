/**
 * @file bug448.cpp bug448 regression case ("Wildcard in host column of mysql.user table don't work properly")
 *
 * Test creates user1@xxx.%.%.% and tries to use it to connect
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "get_my_ip.h"

int main()
{
    char my_ip[16];
    char sql[1024];
    char * first_dot;
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    get_my_ip(Test->Maxscale_IP, my_ip);
    printf("Test machine IP %s\n", my_ip);

    first_dot = strstr(my_ip, ".");
    strcpy(first_dot, ".%.%.%");

    printf("Test machine IP with %% %s\n", my_ip);


    printf("Creating user 'user' with 3 different passwords for %s host\n", my_ip);  fflush(stdout);
    sprintf(sql, "GRANT ALL PRIVILEGES ON *.* TO user1@'%s' identified by 'pass1';  FLUSH PRIVILEGES;", my_ip);

    global_result += execute_query(Test->conn_rwsplit, sql);

    MYSQL * conn = open_conn(Test->rwsplit_port, Test->Maxscale_IP, (char *) "user1", (char *) "pass1");
    if (conn == NULL) {
        printf("Authentification failed!\n");
        global_result++;
    } else {
        printf("Authentification for user@'%s' is ok", my_ip);
        mysql_close(conn);
    }

    sprintf(sql, "DROP USER user1@'%s';  FLUSH PRIVILEGES;", my_ip);
    global_result += execute_query(Test->conn_rwsplit, sql);

    Test->CloseMaxscaleConn();

    CheckMaxscaleAlive();

    return(global_result);
}
