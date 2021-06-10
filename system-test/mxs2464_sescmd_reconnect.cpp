/**
 * MXS-2464: Crash in route_stored_query with ReadWriteSplit
 * https://jira.mariadb.org/browse/MXS-2464
 */

#include <maxtest/testconnections.hh>

void run_test(TestConnections& test, const char* query)
{
    test.maxscale->connect_rwsplit();
    std::thread thr([&]() {
                        sleep(5);
                        test.tprintf("block node 0");
                        test.repl->block_node(0);
                        test.tprintf("wait for monitor");
                        test.maxscale->wait_for_monitor(2);
                        test.tprintf("unblock node 0");
                        test.repl->unblock_node(0);
                    });

    test.reset_timeout();
    test.tprintf("%s", query);
    test.try_query(test.maxscale->conn_rwsplit[0], "%s", query);

    test.tprintf("disconnect");
    test.maxscale->disconnect();
    test.tprintf("join");
    thr.join();
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    run_test(test, "SET @a = (SELECT SLEEP(10))");

    test.repl->connect();
    auto master_id = test.repl->get_server_id_str(0);
    test.repl->disconnect();

    std::string query = "SET @a = (SELECT SLEEP(CASE @@server_id WHEN " + master_id + " THEN 10 ELSE 0 END))";
    run_test(test, query.c_str());

    return test.global_result;
}
