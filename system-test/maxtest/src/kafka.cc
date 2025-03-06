/*
 * Copyright (c) 2024 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/kafka.hh>
#include <iostream>
#include <maxbase/format.hh>
Kafka::Kafka(TestConnections& test)
    : m_test(test)
    , m_docker(
        test,
        "apache/kafka",
        "kafka",
        {4008},
{
    "KAFKA_LISTENERS=PLAINTEXT://0.0.0.0:4008,CONTROLLER://0.0.0.0:9093",
    "KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://"s + test.maxscale->ip4() + ":4008,CONTROLLER://" + test.maxscale->ip4() + ":9093",
    "KAFKA_NODE_ID=1",
    "KAFKA_PROCESS_ROLES=broker,controller",
    "KAFKA_CONTROLLER_LISTENER_NAMES=CONTROLLER",
    "KAFKA_LISTENER_SECURITY_PROTOCOL_MAP=CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT",
    "KAFKA_CONTROLLER_QUORUM_VOTERS=1@localhost:9093",
    "KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1",
    "KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR=1",
    "KAFKA_TRANSACTION_STATE_LOG_MIN_ISR=1",
    "KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS=0",
    "KAFKA_NUM_PARTITIONS=1"
},
        "/opt/kafka/bin/kafka-topics.sh --bootstrap-server 127.0.0.1:4008 --list"
        )
{
}

void Kafka::create_topic(const std::string& topic)
{
    std::string cmd = "/opt/kafka/bin/kafka-topics.sh --create --topic " + topic
        + " --bootstrap-server 127.0.0.1:4008";

    m_test.expect(m_docker.execute(cmd) == 0,
                  "Failed to create topic '%s'", topic.c_str());
}

Logger::Logger(TestConnections& test)
    : m_test(test)
{
}

void Logger::event_cb(RdKafka::Event& event)
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

Consumer::Consumer(TestConnections& test, const std::string& subscription)
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

int Consumer::consume_messages()
{
    int i = 0;

    while (consume_one_message()->err() == RdKafka::ERR_NO_ERROR)
    {
        ++i;
    }

    return i;
}

int Consumer::try_consume_messages(int n_expected)
{
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    const std::chrono::seconds limit {30};
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
        else if (err != RdKafka::ERR_REQUEST_TIMED_OUT && err != RdKafka::ERR__TIMED_OUT)
        {
            std::cout << "Error from Kafka: " << RdKafka::err2str(err) << std::endl;
            break;
        }
    }

    return i;
}

void Consumer::commit()
{
    auto err = m_consumer->commitSync();

    if (err != RdKafka::ERR_NO_ERROR)
    {
        std::cout << "Failed to commit offsets: " << RdKafka::err2str(err) << std::endl;
    }
}

Producer::Producer(TestConnections& test)
    : m_test(test)
    , m_logger(test)
{
    std::string err;
    std::unique_ptr<RdKafka::Conf> cnf {RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
    cnf->set("bootstrap.servers", test.maxscale->ip4() + std::string(":4008"), err);
    cnf->set("event_cb", &m_logger, err);
    m_producer.reset(RdKafka::Producer::create(cnf.get(), err));
}

bool Producer::produce_message(const std::string& topic, const std::string& key, const std::string& value)
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
