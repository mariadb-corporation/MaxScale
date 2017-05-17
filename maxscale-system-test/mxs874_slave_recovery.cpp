/**
 * @file mxs874_slave_recovery.cpp Block and unblock first and second slaves and check that they are recovered
 * - Start MaxScale with 1 master and 2 slaves
 * - Connect to MaxScale with Readwritesplit
 * - Execute SET @a=1
 * - Block first slave
 * - Wait until monitor detects it
 * - Unblock first slave and block the second slave
 * - Check that first slave is recovered
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->connect_maxscale();

    Test->set_timeout(10);
    Test->try_query(Test->conn_rwsplit, (char *) "SET @a=1");
    Test->stop_timeout();
    sleep(1);
    Test->set_timeout(20);
    Test->tprintf("Blocking first slave\n");
    Test->repl->block_node(1);
    Test->stop_timeout();
    sleep(5);
    Test->set_timeout(10);
    Test->tprintf("Unblocking first slave and blocking second slave\n");

    Test->repl->unblock_node(1);
    Test->stop_timeout();
    sleep(5);
    Test->repl->block_node(2);
    Test->stop_timeout();
    sleep(5);
    Test->set_timeout(20);

    int retries;

    for (retries = 0; retries < 10; retries++)
    {
        char server1_status[256];
        Test->get_maxadmin_param((char *) "show server server2", (char *) "Status", server1_status);
        if (strstr(server1_status, "Running"))
        {
            break;
        }
        sleep(1);
    }

    Test->add_result(retries == 10, "Slave is not recovered, slave status is not Running\n");

    Test->repl->connect();
    int real_id = Test->repl->get_server_id(1);

    char server_id[200] = "";
    find_field(Test->conn_rwsplit, "SELECT @@server_id", "@@server_id", server_id);
    int queried_id = atoi(server_id);

    Test->add_result(queried_id != real_id, "The query server ID '%d' does not match the one from server '%d'. "
                     "Slave was not recovered.", queried_id, real_id);

    char userval[200] = "";
    find_field(Test->conn_rwsplit, "SELECT @a", "@a", userval);

    Test->add_result(atoi(userval) != 1, "User variable @a is not 1, it is '%s'", userval);

    Test->tprintf("Unblocking second slave\n");
    Test->repl->unblock_node(2);

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
