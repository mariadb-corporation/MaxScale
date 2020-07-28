/**
 * MXS-2464: Crash in route_stored_query with ReadWriteSplit
 * https://jira.mariadb.org/browse/MXS-2464
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.maxscales->connect();
    std::thread thr([&]() {
                        sleep(5);
                        test.tprintf("block node 0");
                        test.repl->block_node(0);
                        test.tprintf("wait for monitor");
                        test.maxscales->wait_for_monitor(2);
                        test.tprintf("unblock node 0");
                        test.repl->unblock_node(0);
                    });

    constexpr const char* query = "SET @a = (SELECT SLEEP(10))";
    test.set_timeout(60);
    test.tprintf("%s", query);
    test.try_query(test.maxscales->conn_rwsplit[0], query);
    test.stop_timeout();

    test.tprintf("disconnect");
    test.maxscales->disconnect();
    test.tprintf("join");
    thr.join();

    return test.global_result;
}
