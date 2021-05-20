#include <maxtest/testconnections.hh>
#include <maxtest/kafka.hh>

using namespace std::literals::string_literals;

void read_messages(TestConnections& test, Consumer& consumer, int n_expected)
{
    int i = consumer.try_consume_messages(n_expected);
    test.expect(i == n_expected, "Expected %d messages, got %d", n_expected, i);
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    Kafka kafka(test);
    kafka.create_topic("test.t1");

    test.repl->stop_slaves();
    auto conn = test.repl->get_connection(0);
    conn.connect();
    conn.query("RESET MASTER");
    test.maxscales->start();

    // Stop B-Monitor, A-Monitor will take ownership of the cluster
    test.maxctrl("stop monitor B-Monitor");
    sleep(5);
    test.maxctrl("start monitor B-Monitor");

    // Connect to Kafka
    Consumer consumer(test, "kafkacdc");

    test.tprintf("Inserting data");
    conn.connect();
    conn.query("RESET MASTER");
    conn.query("CREATE TABLE t1(id INT)");
    conn.query("INSERT INTO t1 VALUES (1), (2), (3)");
    conn.query("UPDATE t1 SET id = 4 WHERE id = 2");
    conn.query("DELETE FROM t1 WHERE id = 3");
    auto gtid = conn.field("SELECT @@gtid_binlog_pos");
    test.tprintf("GTID: %s", gtid.c_str());

    test.tprintf("Give MaxScale some time to process the events");
    sleep(5);

    read_messages(test, consumer, 7);

    // Pass ownership to B-Monitor
    test.maxctrl("stop monitor A-Monitor");
    sleep(5);
    test.maxctrl("start monitor A-Monitor");

    conn.query("INSERT INTO t1 VALUES (4), (5), (6)");
    gtid = conn.field("SELECT @@gtid_binlog_pos");
    test.tprintf("GTID: %s", gtid.c_str());
    sleep(5);

    read_messages(test, consumer, 3);

    // Stop both monitors, no events should be sent
    test.maxctrl("stop monitor A-Monitor");
    test.maxctrl("stop monitor B-Monitor");
    sleep(5);

    conn.query("INSERT INTO t1 VALUES (7), (8), (9)");
    gtid = conn.field("SELECT @@gtid_binlog_pos");
    test.tprintf("GTID: %s", gtid.c_str());
    sleep(5);

    read_messages(test, consumer, 0);
    test.repl->fix_replication();

    return test.global_result;
}
