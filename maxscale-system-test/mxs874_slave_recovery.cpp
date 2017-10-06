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
    TestConnections test(argc, argv);
    test.set_timeout(10);

    test.maxscales->connect_maxscale(0);

    test.set_timeout(10);
    test.try_query(test.maxscales->conn_rwsplit[0], (char *) "SET @a=1");
    test.stop_timeout();
    sleep(1);
    test.set_timeout(20);
    test.tprintf("Blocking first slave\n");
    test.repl->block_node(1);
    test.stop_timeout();
    sleep(5);
    test.set_timeout(10);
    test.tprintf("Unblocking first slave and blocking second slave\n");

    test.repl->unblock_node(1);
    test.stop_timeout();
    sleep(5);
    test.repl->block_node(2);
    test.stop_timeout();
    sleep(5);
    test.set_timeout(20);

    int retries;

    for (retries = 0; retries < 10; retries++)
    {
        char server1_status[256];
        test.maxscales->get_maxadmin_param(0, (char *) "show server server2", (char *) "Status", server1_status);
        if (strstr(server1_status, "Running"))
        {
            break;
        }
        sleep(1);
    }

    test.add_result(retries == 10, "Slave is not recovered, slave status is not Running\n");

    test.repl->connect();
    int real_id = test.repl->get_server_id(1);

    char server_id[200] = "";
    find_field(test.maxscales->conn_rwsplit[0], "SELECT @@server_id", "@@server_id", server_id);
    int queried_id = atoi(server_id);

    test.add_result(queried_id != real_id, "The query server ID '%d' does not match the one from server '%d'. "
                     "Slave was not recovered.", queried_id, real_id);

    char userval[200] = "";
    find_field(test.maxscales->conn_rwsplit[0], "SELECT @a", "@a", userval);

    test.add_result(atoi(userval) != 1, "User variable @a is not 1, it is '%s'", userval);

    test.tprintf("Unblocking second slave\n");
    test.repl->unblock_node(2);

    test.check_maxscale_alive(0);
    return test.global_result;
}
