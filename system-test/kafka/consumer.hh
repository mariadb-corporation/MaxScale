#include <future>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <maxtest/testconnections.hh>

class Consumer
{
public:

    Consumer(TestConnections& test)
    {
        using namespace std::literals::string_literals;
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
