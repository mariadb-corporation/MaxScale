#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    printf("Creating user 'user' \n");  fflush(stdout);

    global_result += execute_query(Test->conn_rwsplit, (char *) "GRANT SELECT ON test.* TO user@'%'  identified by 'pass2';  FLUSH PRIVILEGES;");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t1; CREATE TABLE t1 (x1 int, fl int)");

    printf("Changing user... \n");  fflush(stdout);
    if (mysql_change_user(Test->conn_rwsplit, (char *) "user", (char *) "pass2", (char *) "test") != 0) {
        global_result++;
        printf("changing user failed \n");  fflush(stdout);
    }

    printf("Trying INSERT (expecting access denied)... \n");  fflush(stdout);
    if ( execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 VALUES (1, 1);") == 0) {
        global_result++;
        printf("INSERT query succedded to user which does not have INSERT PRIVILEGES\n"); fflush(stdout);
    }

    printf("Changing user back... \n");  fflush(stdout);
    if (mysql_change_user(Test->conn_rwsplit, Test->repl->User, Test->repl->Password, (char *) "test") != 0) {
        global_result++;
        printf("changing user failed \n");  fflush(stdout);
    }

    printf("Trying INSERT (expecting success)... \n");  fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 VALUES (1, 1);");

    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'%';");

    Test->CloseMaxscaleConn();

    return(global_result);

}

