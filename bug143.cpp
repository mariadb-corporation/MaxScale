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

    printf("Creating user 'user' with 3 different passwords for different hosts\n");  fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "GRANT ALL PRIVILEGES ON *.* TO user@'non_existing_host1' identified by 'pass1';  FLUSH PRIVILEGES;");
    execute_query(Test->conn_rwsplit, (char *) "GRANT ALL PRIVILEGES ON *.* TO user@'%'  identified by 'pass2';  FLUSH PRIVILEGES;");
    execute_query(Test->conn_rwsplit, (char *) "GRANT ALL PRIVILEGES ON *.* TO user@'non_existing_host2' identified by 'pass3';  FLUSH PRIVILEGES;");

    printf("sleeping 60 seconds to let replication happen\n");  fflush(stdout);

    sleep(60);

    MYSQL * conn = open_conn(Test->rwsplit_port, Test->Maxscale_IP, (char *) "user", (char *) "pass1");
    if (conn != NULL) {
        printf("MaxScale ignores host in authentification\n");
        global_result++;
        mysql_close(conn);
    }

    conn = open_conn(Test->rwsplit_port, Test->Maxscale_IP, (char *) "user", (char *) "pass2");
    if (conn == NULL) {
        printf("MaxScale can't connect\n");
        global_result++;
    }
    else {
        mysql_close(conn);
    }

    conn = open_conn(Test->rwsplit_port, Test->Maxscale_IP, (char *) "user", (char *) "pass3");
    if (conn != NULL) {
        printf("MaxScale ignores host in authentification\n");
        global_result++;
        mysql_close(conn);
    }

    execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'%';");
    execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'non_existing_host1';");
    execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'non_existing_host2';");
    Test->CloseMaxscaleConn();

    return(global_result);

}
