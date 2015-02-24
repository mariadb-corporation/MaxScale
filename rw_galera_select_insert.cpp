/**
 * @file rw_galera_select_insert.cpp NOT IMPLEMENTET YET
 *
 */

#include <my_config.h>
#include "testconnections.h"
#include "get_com_select_insert.h"
#include "maxadmin_operations.h"

int selects[256];
int inserts[256];
int new_selects[256];
int new_inserts[256];
int silent = 0;
int tolerance;

int main(int argc, char *argv[])
{
    int global_result = 0;

    TestConnections * Test = new TestConnections(argv[0]);
    Test->ReadEnv();
    Test->PrintIP();
    Test->galera->Connect();

    tolerance=0;

    // connect to the MaxScale server (rwsplit)
    Test->ConnectRWSplit();

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "shutdown monitor \"Galera Monitor\"");

    if (Test->conn_rwsplit == NULL ) {
        printf("Can't connect to MaxScale\n");
        exit(1);
    } else {

        global_result += execute_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS t1;");
        global_result += execute_query(Test->conn_rwsplit, "create table t1 (x1 int);");

        get_global_status_allnodes(&selects[0], &inserts[0], Test->galera, silent);
        global_result += execute_query(Test->conn_rwsplit, "select * from t1;");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->galera, silent);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->galera->N);

        global_result += execute_query(Test->conn_rwsplit, "insert into t1 values(1);");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->galera, silent);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->galera->N);

        // close connections

        Test->CloseRWSplit();
    }
    Test->galera->CloseConn();

    Test->Copy_all_logs(); return(global_result);
}
