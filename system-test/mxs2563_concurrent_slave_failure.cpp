/**
 * MXS-2563: Failing debug assertion at rwsplitsession.cc:1129 : m_expected_responses == 0
 * https://jira.mariadb.org/browse/MXS-2563
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.maxctrl("alter monitor MariaDB-Monitor monitor_interval 99999ms");

    auto conn = test.maxscale->rwsplit();
    conn.connect();
    conn.query("SET @a = (SELECT SLEEP(1))");

    std::thread thr(
        [&test]() {
            sleep(5);
            test.repl->stop_node(2);
            test.repl->stop_node(3);
            sleep(5);
            test.repl->start_node(2);
            test.repl->start_node(3);
        });
    // Should go to server2
    conn.query("SELECT SLEEP(20)");
    thr.join();


    test.maxctrl("alter monitor MariaDB-Monitor monitor_interval 1000ms");

    return test.global_result;
}
