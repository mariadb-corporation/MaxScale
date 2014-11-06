#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int i;
    int N=4;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    if (Test->ConnectMaxscale() !=0 ) {
        printf("Error connecting to MaxScale\n");
        exit(1);
    }

    create_t1(Test->conn_rwsplit);
    insert_into_t1(Test->conn_rwsplit, N);

    global_result += execute_query(Test->conn_rwsplit, (char *) "PREPARE stmt FROM 'SELECT * FROM t1 WHERE fl=@x;';");
    global_result += execute_query(Test->conn_rwsplit, (char *) "SET @x = 1;';");
    global_result += execute_query(Test->conn_rwsplit, (char *) "EXECUTE stmt");
    global_result += execute_query(Test->conn_rwsplit, (char *) "SET @x = 2;';");
    global_result += execute_query(Test->conn_rwsplit, (char *) "EXECUTE stmt");

    global_result += CheckMaxscaleAlive();
    exit(global_result);
}
