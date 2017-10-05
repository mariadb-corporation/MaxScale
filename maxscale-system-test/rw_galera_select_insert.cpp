/**
 * @file rw_galera_select_insert.cpp NOT IMPLEMENTET YET
 *
 */


#include "testconnections.h"
#include "get_com_select_insert.h"
#include "maxadmin_operations.h"

long int selects[256];
long int inserts[256];
long int new_selects[256];
long int new_inserts[256];
int silent = 0;
int tolerance;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    Test->galera->connect();

    tolerance = 0;

    // connect to the MaxScale server (rwsplit)
    Test->connect_rwsplit();

    Test->execute_maxadmin_command((char *) "shutdown monitor \"Galera Monitor\"");

    if (Test->maxscales->conn_rwsplit[0] == NULL )
    {
        Test->add_result(1, "Can't connect to MaxScale\n");
        int rval = Test->global_result;
        delete Test;
        exit(1);
    }
    else
    {

        Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS t1;");
        Test->try_query(Test->maxscales->conn_rwsplit[0], "create table t1 (x1 int);");

        get_global_status_allnodes(&selects[0], &inserts[0], Test->galera, silent);
        Test->try_query(Test->maxscales->conn_rwsplit[0], "select * from t1;");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->galera, silent);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->galera->N);

        Test->try_query(Test->maxscales->conn_rwsplit[0], "insert into t1 values(1);");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->galera, silent);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->galera->N);

        // close connections
        Test->close_rwsplit();
    }
    Test->galera->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
