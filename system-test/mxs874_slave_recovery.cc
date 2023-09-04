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
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.reset_timeout();

    test.maxscale->connect_maxscale();

    test.reset_timeout();
    test.try_query(test.maxscale->conn_rwsplit, (char*) "SET @a=1");
    sleep(1);
    test.reset_timeout();
    test.tprintf("Blocking first slave\n");
    test.repl->block_node(1);
    sleep(5);
    test.reset_timeout();
    test.tprintf("Unblocking first slave and blocking second slave\n");

    test.repl->unblock_node(1);
    sleep(5);
    test.repl->block_node(2);
    sleep(5);
    test.reset_timeout();

    for (int retries = 0; test.get_server_status("server2").count("Running") == 0 && retries < 10; retries++)
    {
    }

    auto status = test.get_server_status("server2");
    test.add_result(status.count("Slave") == 0,
                    "Slave is not recovered, slave status is not Running: %s", mxb::join(status).c_str());

    test.repl->connect();
    int real_id = test.repl->get_server_id(1);

    char server_id[200] = "";
    find_field(test.maxscale->conn_rwsplit, "SELECT @@server_id", "@@server_id", server_id);
    int queried_id = atoi(server_id);

    test.add_result(queried_id != real_id,
                    "The query server ID '%d' does not match the one from server '%d'. "
                    "Slave was not recovered.",
                    queried_id,
                    real_id);

    char userval[200] = "";
    find_field(test.maxscale->conn_rwsplit, "SELECT @a", "@a", userval);

    test.add_result(atoi(userval) != 1, "User variable @a is not 1, it is '%s'", userval);

    test.tprintf("Unblocking second slave\n");
    test.repl->unblock_node(2);

    test.check_maxscale_alive();
    return test.global_result;
}
