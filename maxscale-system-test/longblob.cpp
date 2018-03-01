/**
 * @file longblob.cpp - trying to use LONGBLOB
 * - try to insert large BLOB, MEDIUMBLOB and LONGBLOB via RWSplit, ReadConn Master and directly to backend
 */


#include "testconnections.h"
#include "blob_test.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(60);

    Test->repl->execute_query_all_nodes( (char *) "set global max_allowed_packet=10000000");

    /*Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("LONGBLOB: Trying send data directly to Master\n");
    test_longblob(Test, Test->repl->nodes[0], (char *) "LONGBLOB", 1000000, 20, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();*/

    Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("LONGBLOB: Trying send data via RWSplit\n");
    test_longblob(Test, Test->conn_rwsplit, (char *) "LONGBLOB", 1000000, 20, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();

    Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("LONGBLOB: Trying send data via ReadConn master\n");
    test_longblob(Test, Test->conn_master, (char *) "LONGBLOB", 1000000, 20, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();



    /*Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("BLOB: Trying send data directly to Master\n");
    test_longblob(Test, Test->repl->nodes[0], (char *) "BLOB", 1000, 8, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();*/

    Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("BLOB: Trying send data via RWSplit\n");
    test_longblob(Test, Test->conn_rwsplit, (char *) "BLOB", 1000, 8, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();

    Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("BLOB: Trying send data via ReadConn master\n");
    test_longblob(Test, Test->conn_master, (char *) "BLOB", 1000, 8, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();


    /*Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("MEDIUMBLOB: Trying send data directly to Master\n");
    test_longblob(Test, Test->repl->nodes[0], (char *) "MEDIUMBLOB", 1000000, 2, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();*/

    Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("MEDIUMBLOB: Trying send data via RWSplit\n");
    test_longblob(Test, Test->conn_rwsplit, (char *) "MEDIUMBLOB", 1000000, 2, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();

    Test->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("MEDIUMBLOB: Trying send data via ReadConn master\n");
    test_longblob(Test, Test->conn_master, (char *) "MEDIUMBLOB", 1000000, 2, 1);
    Test->repl->close_connections();
    Test->close_maxscale_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
