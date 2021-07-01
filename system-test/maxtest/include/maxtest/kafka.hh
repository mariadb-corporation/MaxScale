#include <future>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

class Kafka
{
public:
    Kafka(TestConnections& test)
        : m_test(test)
    {
        if (m_test.maxscale->ssh_node_f(false, "test -d kafka") != 0)
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

    void create_topic(const std::string& topic)
    {
        std::string cmd = "kafka/bin/kafka-topics.sh --create --topic " + topic
            + " --bootstrap-server 127.0.0.1:4008";

        m_test.expect(m_test.maxscale->ssh_node_f(false, "%s", cmd.c_str()) == 0,
                      "Failed to create topic '%s'", topic.c_str());
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
            " --override listeners=PLAINTEXT://0.0.0.0:4008"
            " --override advertised.listeners=PLAINTEXT://%s:4008;",
            m_test.maxscale->ip4());

        std::string check =
            "kafka/bin/zookeeper-shell.sh localhost:2181"
            " ls -R /brokers/ids|grep /brokers/ids/0";

        if (m_test.maxscale->ssh_node_f(false, "%s", (zookeeper + kafka).c_str()) == 0)
        {
            for (int i = 0; i < 10; i++)
            {
                if (m_test.maxscale->ssh_node_f(false, "%s", check.c_str()) == 0)
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
        m_test.maxscale->ssh_node_f(
            false,
            "kafka/bin/kafka-server-stop.sh;"
            "kafka/bin/zookeeper-server-stop.sh;"
            "rm -rf /tmp/zookeeper /tmp/kafka-logs;");
    }

    bool install_kafka()
    {
        // Download the package locally, wget isn't always installed on the MaxScale VM.
        std::string download =
            R"EOF(
wget -q "https://www.apache.org/dyn/closer.cgi?filename=/kafka/2.7.0/kafka_2.13-2.7.0.tgz&action=download" -O kafka_2.13-2.7.0.tgz;
)EOF";

        if (system(download.c_str()) != 0)
        {
            m_test.add_failure("Failed to wget kafka sources.");
            return false;
        }

        if (!m_test.maxscale->copy_to_node("./kafka_2.13-2.7.0.tgz", "~/kafka_2.13-2.7.0.tgz"))
        {
            m_test.add_failure("Failed to copy kafka sources to node.");
            return false;
        }

        // TODO: Install java with mdbci, this is a dumb workaround that will fail at some point.
        if (m_test.maxscale->ssh_node_f(false, "sudo yum -y install java-latest-openjdk;") != 0)
        {
            m_test.add_failure("Failed to install java-latest-openjdk");
            return false;
        }

        // The link can be updated by getting the closest mirror link from the Kafka download page and
        // changing `file` to `filename` and adding `action=download` (these are options to closer.cgi).
        std::string command =
            R"EOF(
tar -axf kafka_2.13-2.7.0.tgz;
rm kafka_2.13-2.7.0.tgz;
mv kafka_2.13-2.7.0 kafka;
        )EOF";

        if (m_test.maxscale->ssh_node_f(false, "%s", command.c_str()) != 0)
        {
            m_test.add_failure("Failed to untar and rename kafka directory.");
            return false;
        }

        return true;
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

    Consumer(TestConnections& test, const std::string& subscription)
        : m_logger(test)
    {
        std::string err;
        std::unique_ptr<RdKafka::Conf> cnf {RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        cnf->set("bootstrap.servers", test.maxscale->ip4() + std::string(":4008"), err);
        cnf->set("group.id", "kafkacdc", err);
        cnf->set("enable.auto.commit", "false", err);
        cnf->set("enable.auto.offset.store", "true", err);
        cnf->set("auto.offset.reset", "smallest", err);
        cnf->set("topic.metadata.refresh.interval.ms", "10000", err);
        cnf->set("event_cb", &m_logger, err);

        m_consumer.reset(RdKafka::KafkaConsumer::create(cnf.get(), err));
        m_consumer->subscribe({subscription});
    }

    ~Consumer()
    {
        m_consumer->close();
    }

    std::unique_ptr<RdKafka::Message> consume_one_message()
    {
        return std::unique_ptr<RdKafka::Message>(m_consumer->consume(10000));
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

    void commit()
    {
        auto err = m_consumer->commitSync();

        if (err != RdKafka::ERR_NO_ERROR)
        {
            std::cout << "Failed to commit offsets: " << RdKafka::err2str(err) << std::endl;
        }
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
        cnf->set("bootstrap.servers", test.maxscale->ip4() + std::string(":4008"), err);
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
