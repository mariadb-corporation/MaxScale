#include <future>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include "testconnections.h"

using namespace std::literals::string_literals;

int consume_messages(TestConnections& test)
{
    std::string err;
    auto cnf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    cnf->set("bootstrap.servers", test.maxscales->IP[0] + ":9092"s, err);
    cnf->set("group.id", "kafkacdc", err);

    auto consumer = RdKafka::KafkaConsumer::create(cnf, err);
    consumer->subscribe({"kafkacdc"});
    int i = 0;
    bool ok = true;

    while (ok)
    {
        auto msg = consumer->consume(10000);

        if (msg->err() == RdKafka::ERR_NO_ERROR)
        {
            std::cout << "Message key: " << *msg->key() << std::endl;
            std::cout << "Message content: " << std::string((char*)msg->payload(), msg->len()) << std::endl;
            i++;
        }
        else
        {
            ok = false;
        }

        delete msg;
    }

    delete consumer;
    delete cnf;

    return i;
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.tprintf("Starting Kafka container");
    test.maxscales->ssh_output(
        "sudo docker run -d -e ADVERTISED_HOST="s + test.maxscales->IP[0]
        + " --network=host --name=kafka spotify/kafka");

    test.repl->stop_slaves();
    auto conn = test.repl->get_connection(0);

    test.tprintf("Inserting data");
    conn.connect();
    conn.query("CREATE TABLE t1(id INT)");
    conn.query("INSERT INTO t1 VALUES (1), (2), (3)");
    conn.query("UPDATE t1 SET id = 4 WHERE id = 2");
    conn.query("DELETE FROM t1 WHERE id = 3");
    auto gtid = conn.field("SELECT @@gtid_binlog_pos");

    test.tprintf("Give MaxScale some time to process the events");
    sleep(5);

    int i = consume_messages(test);
    const int n_expected = 7;
    test.expect(i == n_expected, "Expected %d messages, got %d", n_expected, i);

    test.tprintf("Stopping Kafka container");
    test.maxscales->ssh_output("sudo docker ps -aq|xargs sudo docker rm -vf");
    test.repl->fix_replication();

    return test.global_result;
}
