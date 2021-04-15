#include <future>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <maxtest/testconnections.hh>

using namespace std::literals::string_literals;

class Kafka
{
public:
    Kafka(TestConnections& test)
        : m_test(test)
    {
        m_test.tprintf("Starting Kafka container");
        auto res = m_test.maxscales->ssh_output(
            "sudo docker run -d -e ADVERTISED_HOST="s + m_test.maxscales->ip4(0)
            + " -p 9092:9092 -p 2182:2181 --network=host --name=kafka spotify/kafka");

        if (res.rc != 0)
        {
            // Try to remove all images in case stale images are left over from old runs. For some reason,
            // sometimes the removal doesn't fully remove the container and a separate remove step is needed.
            m_test.maxscales->ssh_output("sudo docker ps -aq | xargs sudo docker rm -vf");
            res = m_test.maxscales->ssh_output(
                "sudo docker run -d -e ADVERTISED_HOST="s + m_test.maxscales->ip4(0)
                + " -p 9092:9092 -p 2182:2181 --network=host --name=kafka spotify/kafka");
        }

        m_test.expect(res.rc == 0, "Failed to start docker container: %s", res.output.c_str());
    }

    ~Kafka()
    {
        m_test.tprintf("Stopping Kafka container");
        auto res = m_test.maxscales->ssh_output("sudo docker rm -vf kafka");
        m_test.expect(res.rc == 0, "Failed to stop docker container: %s", res.output.c_str());
    }

private:
    TestConnections& m_test;
};

class Consumer
{
public:

    Consumer(TestConnections& test)
    {
        std::string err;
        std::unique_ptr<RdKafka::Conf> cnf {RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        cnf->set("bootstrap.servers", test.maxscales->ip4(0) + ":9092"s, err);
        cnf->set("group.id", "kafkacdc", err);

        m_consumer.reset(RdKafka::KafkaConsumer::create(cnf.get(), err));
        std::unique_ptr<RdKafka::TopicPartition> topic {RdKafka::TopicPartition::create("kafkacdc", 0)};
        topic->set_offset(RdKafka::Topic::OFFSET_BEGINNING);
        m_consumer->assign({topic.get()});
    }

    int consume_messages()
    {
        int i = 0;
        bool ok = true;

        while (ok)
        {
            auto msg = m_consumer->consume(10000);

            if (msg->err() == RdKafka::ERR_NO_ERROR)
            {
                std::cout << "Message key: " << *msg->key() << std::endl;
                std::cout << "Message content: "
                          << std::string((char*)msg->payload(), msg->len()) << std::endl;
                i++;
            }
            else
            {
                ok = false;
            }

            delete msg;
        }

        return i;
    }

private:
    std::unique_ptr<RdKafka::KafkaConsumer> m_consumer;
};

class Producer
{
public:

    Producer(TestConnections& test)
    {
        std::string err;
        std::unique_ptr<RdKafka::Conf> cnf {RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        cnf->set("bootstrap.servers", test.maxscales->ip4(0) + ":9092"s, err);
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
                std::cout << "Error: " << RdKafka::err2str(err) << std::endl;
                ok = false;
                break;
            }
        }
        while (err == RdKafka::ERR__QUEUE_FULL);

        return ok;
    }

private:
    std::unique_ptr<RdKafka::Producer> m_producer;
};
