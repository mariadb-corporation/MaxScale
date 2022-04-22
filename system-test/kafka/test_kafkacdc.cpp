#include <maxtest/testconnections.hh>
#include <maxtest/kafka.hh>
#include <maxbase/json.hh>

using namespace std::literals::string_literals;

void read_messages(TestConnections& test, Consumer& consumer, int n_expected)
{
    int i = consumer.try_consume_messages(n_expected);
    test.expect(i == n_expected, "Expected %d messages, got %d", n_expected, i);
}

mxb::Json get_json(TestConnections& test, Consumer& consumer)
{
    mxb::Json js;

    if (auto msg = consumer.consume_one_message())
    {
        test.expect(js.load_string(std::string((const char*)msg->payload(), msg->len())),
                    "Failed to read JSON from message: %s", js.error_msg().c_str());
    }
    else
    {
        test.expect(false, "Failed to consume message.");
    }

    return js;
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    Kafka kafka(test);
    kafka.create_topic("kafkacdc");

    test.repl->stop_slaves();
    auto conn = test.repl->get_connection(0);
    test.maxscale->start();

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

    test.tprintf("Give MaxScale some time to process the events");
    sleep(5);

    read_messages(test, consumer, 7);

    conn.query("INSERT INTO t1 VALUES (4), (5), (6)");
    sleep(5);

    read_messages(test, consumer, 3);

    test.tprintf("Restarting MaxScale and inserting data");
    test.maxscale->stop();
    test.maxscale->ssh_output("rm /var/lib/maxscale/Kafka-CDC/current_gtid.txt");
    test.maxscale->start();

    conn.query("INSERT INTO t1 VALUES (7), (8), (9)");
    sleep(5);

    read_messages(test, consumer, 3);

    test.tprintf("Enable match and exclude");
    test.maxscale->stop();
    test.maxscale->ssh_output("sed -i -e \"$ a match=cat\" -e \"$ a exclude=bob\" /etc/maxscale.cnf", true);
    conn.query("CREATE TABLE bob(id INT)");
    conn.query("INSERT INTO bob VALUES (10)");
    conn.query("CREATE TABLE bobcat(id INT)");
    conn.query("INSERT INTO bobcat VALUES (11)");
    conn.query("CREATE TABLE cat(id INT)");
    conn.query("INSERT INTO cat VALUES (12)");


    test.maxscale->start();
    sleep(5);

    auto js = get_json(test, consumer);
    test.expect(js.get_string("table") == "cat",
                "Expected schema event: %s", js.to_string().c_str());

    js = get_json(test, consumer);
    test.expect(js.get_string("table_name") == "cat",
                "Expected data event: %s", js.to_string().c_str());
    test.expect(js.get_int("id") == 12,
                "Expected data to be 12: %s", js.to_string().c_str());

    conn.query("DROP TABLE bob");
    conn.query("DROP TABLE bobcat");
    conn.query("DROP TABLE cat");
    conn.query("DROP TABLE t1");

    return test.global_result;
}
