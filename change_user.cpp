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

    printf("Changing user with wrong password... \n");  fflush(stdout);
    if (mysql_change_user(Test->conn_rwsplit, (char *) "user", (char *) "wrong_pass2", (char *) "test") == 0) {
        global_result++;
        printf("FAILED: changing user with wrong password successed! \n");  fflush(stdout);
    }
    printf("%s\n", mysql_error(Test->conn_rwsplit)); fflush(stdout);
    if ((strstr(mysql_error(Test->conn_rwsplit), "Access denied for user")) == NULL) {
        global_result++;
        printf("There is no proper error message\n");

    }

    printf("Trying INSERT again (expecting success - use change should fail)... \n");  fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 VALUES (1, 1);");


    printf("Changing user with wrong password using ReadConn \n");  fflush(stdout);
    if (mysql_change_user(Test->conn_slave, (char *) "user", (char *) "wrong_pass2", (char *) "test") == 0) {
        global_result++;
        printf("FAILED: changing user with wrong password successed! \n");  fflush(stdout);
    }
    printf("%s\n", mysql_error(Test->conn_slave)); fflush(stdout);
    if ((strstr(mysql_error(Test->conn_slave), "Access denied for user")) == NULL) {
        global_result++;
        printf("There is no proper error message\n");
    }

    printf("Changing user for ReadConn \n");  fflush(stdout);
    if (mysql_change_user(Test->conn_slave, (char *) "user", (char *) "pass2", (char *) "test") != 0) {
        global_result++;
        printf("changing user failed \n");  fflush(stdout);
    }


    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'%';");

    Test->CloseMaxscaleConn();

    return(global_result);

}

