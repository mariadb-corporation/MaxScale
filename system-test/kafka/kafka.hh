#include <future>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using namespace std::literals::string_literals;

class Kafka
{
public:
    Kafka(TestConnections& test)
        : m_test(test)
    {
        if (m_test.maxscales->ssh_node_f(0, false, "test -d kafka") != 0)
        {
            if (!install_kafka())
            {
                m_test.add_failure("Failed to install Kafka");
            }
        }

        if (!start_kafka())
        {
            m_test.add_failure("Failed to start Kafka");
        }
    }

    ~Kafka()
    {
        stop_kafka();
    }

private:
    bool start_kafka()
    {
        bool ok = false;
        // Stop any running instances of Kafka and clean out their data directories.
        stop_kafka();

        std::string zookeeper = "kafka/bin/zookeeper-server-start.sh "
                                "-daemon kafka/config/zookeeper.properties;";
        std::string kafka = mxb::string_printf(
            "kafka/bin/kafka-server-start.sh"
            " -daemon kafka/config/server.properties"
            " --override listeners=PLAINTEXT://0.0.0.0:9092"
            " --override advertised.listeners=PLAINTEXT://%s:9092;",
            m_test.maxscales->ip4(0));

        std::string check =
            "kafka/bin/zookeeper-shell.sh localhost:2181"
            " ls -R /brokers/ids|grep /brokers/ids/0";

        if (m_test.maxscales->ssh_node_f(0, false, "%s", (zookeeper + kafka).c_str()) == 0)
        {
            for (int i = 0; i < 10; i++)
            {
                if (m_test.maxscales->ssh_node_f(0, false, "%s", check.c_str()) == 0)
                {
                    ok = true;
                    break;
                }
                else
                {
                    sleep(1);
                }
            }
        }

        return ok;
    }

    void stop_kafka()
    {
        m_test.maxscales->ssh_node_f(
            0, false,
            "kafka/bin/kafka-server-stop.sh;"
            "kafka/bin/zookeeper-server-stop.sh;"
            "rm -rf /tmp/zookeeper /tmp/kafka-logs;");
    }

    bool install_kafka()
    {
        // The link can be updated by getting the closest mirror link from the Kafka download page and
        // changing `file` to `filename` and adding `action=download` (these are options to closer.cgi).
        std::string command =
            R"EOF(
wget -q "https://www.apache.org/dyn/closer.cgi?filename=/kafka/2.7.0/kafka_2.13-2.7.0.tgz&action=download" -O kafka_2.13-2.7.0.tgz;
tar -axf kafka_2.13-2.7.0.tgz;
rm kafka_2.13-2.7.0.tgz;
mv kafka_2.13-2.7.0 kafka;
        )EOF";

        return m_test.maxscales->ssh_node_f(0, false, "%s", command.c_str()) == 0;
    }

    TestConnections& m_test;
};

class Logger : public RdKafka::EventCb
{
public:
    Logger(TestConnections& test)
        : m_test(test)
    {
    }

    void event_cb(RdKafka::Event& event) override
    {
        switch (event.type())
        {
        case RdKafka::Event::EVENT_LOG:
            m_test.tprintf("%s", event.str().c_str());
            break;

        case RdKafka::Event::EVENT_ERROR:
            m_test.tprintf("%s", RdKafka::err2str(event.err()).c_str());
            break;

        default:
            m_test.tprintf("%s", event.str().c_str());
            break;
        }
    }

private:
    TestConnections& m_test;
};

class Consumer
{
public:

    Consumer(TestConnections& test)
        : m_logger(test)
    {
        std::string err;
        std::unique_ptr<RdKafka::Conf> cnf {RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        cnf->set("bootstrap.servers", test.maxscales->ip4(0) + ":9092"s, err);
        cnf->set("group.id", "kafkacdc", err);
        cnf->set("event_cb", &m_logger, err);

        m_consumer.reset(RdKafka::KafkaConsumer::create(cnf.get(), err));
        std::unique_ptr<RdKafka::TopicPartition> topic {RdKafka::TopicPartition::create("kafkacdc", 0)};
        topic->set_offset(RdKafka::Topic::OFFSET_BEGINNING);
        m_consumer->assign({topic.get()});
    }

    std::unique_ptr<RdKafka::Message> consume_one_message()
    {
        std::unique_ptr<RdKafka::Message> msg(m_consumer->consume(10000));

        if (msg->err() == RdKafka::ERR_NO_ERROR)
        {
            std::string key = msg->key() ? *msg->key() : "";
            std::string payload((char*)msg->payload(), msg->len());
            std::cout << "Message key: " << key << std::endl;
            std::cout << "Message content: " << payload << std::endl;
        }

        return msg;
    }

    int consume_messages()
    {
        int i = 0;

        while (consume_one_message()->err() == RdKafka::ERR_NO_ERROR)
        {
            ++i;
        }

        return i;
    }


    int try_consume_messages(int n_expected)
    {
        using Clock = std::chrono::steady_clock;
        auto start = Clock::now();
        const std::chrono::seconds limit{30};
        int i = 0;

        while (i < n_expected && Clock::now() - start < limit)
        {
            auto err = consume_one_message()->err();

            if (err == RdKafka::ERR_NO_ERROR)
            {
                ++i;
            }
            else if (err == RdKafka::ERR_UNKNOWN_TOPIC_OR_PART)
            {
                // Topic doesn't exist yet, sleep for a few seconds
                sleep(5);
            }
            else if (err != RdKafka::ERR_REQUEST_TIMED_OUT || err != RdKafka::ERR__TIMED_OUT)
            {
                std::cout << "Error from Kafka: " << RdKafka::err2str(err) << std::endl;
                break;
            }
        }

        return i;
    }

private:
    std::unique_ptr<RdKafka::KafkaConsumer> m_consumer;
    Logger                                  m_logger;
};

class Producer
{
public:

    Producer(TestConnections& test)
        : m_test(test)
        , m_logger(test)
    {
        std::string err;
        std::unique_ptr<RdKafka::Conf> cnf {RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        cnf->set("bootstrap.servers", test.maxscales->ip4(0) + ":9092"s, err);
        cnf->set("event_cb", &m_logger, err);
        m_producer.reset(RdKafka::Producer::create(cnf.get(), err));
    }

    bool produce_message(const std::string& topic, const std::string& key, const std::string& value)
    {
        bool ok = true;
        RdKafka::ErrorCode err;

        do
        {
            err = m_producer->produce(
                topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                (void*)value.c_str(), value.length(), key.c_str(), key.length(), 0, nullptr);

            if (err == RdKafka::ERR__QUEUE_FULL)
            {
                m_producer->poll(1000);
            }
            else if (err != RdKafka::ERR_NO_ERROR)
            {
                m_test.logger().add_failure("Failed to produce message: %s", RdKafka::err2str(err).c_str());
                ok = false;
                break;
            }
        }
        while (err == RdKafka::ERR__QUEUE_FULL);

        return ok;
    }

    void flush()
    {
        m_producer->flush(10000);
    }

private:
    TestConnections&                   m_test;
    std::unique_ptr<RdKafka::Producer> m_producer;
    Logger                             m_logger;
};
