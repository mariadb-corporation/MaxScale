/**
 * @file bug656.cpp Checks Maxscale behaviour in case if Master node is blocked - NOT NEEDED BECAUSE IT IS ALREADY CHECKED BY OTHER TESTS!!!!
 *
 * - ConnecT to RWSplit
 * - block Mariadb server on Master node by Firewall
 * - try simple query *show servers" via Maxadmin
 */


#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscales->IP[0]);
    Test->maxscales->connect_rwsplit(0);

    Test->tprintf("Setup firewall to block mysql on master\n");
    Test->repl->block_node(0);

    //printf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    //execute_query(Test->maxscales->conn_rwsplit[0], (char *) "show processlist;");
    execute_maxadmin_command_print(Test->maxscales->IP[0], (char *) "admin", Test->maxscales->maxadmin_password[0],
                                   (char *) "show servers");

    Test->tprintf("Setup firewall back to allow mysql and wait\n");
    Test->repl->unblock_node(0);
    sleep(10);

    Test->maxscales->close_rwsplit(0);

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}


