/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <librdkafka/rdkafkacpp.h>
#include <maxtest/testconnections.hh>

class Kafka
{
public:
    Kafka(TestConnections& test);

    void create_topic(const std::string& topic);

    ~Kafka()
    {
        stop_kafka();
    }

private:
    bool start_kafka();
    void stop_kafka();
    bool install_kafka();

    TestConnections& m_test;
};

class Logger : public RdKafka::EventCb
{
public:
    Logger(TestConnections& test);

    void event_cb(RdKafka::Event& event) override;

private:
    TestConnections& m_test;
};

class Consumer
{
public:

    Consumer(TestConnections& test, const std::string& subscription);

    ~Consumer()
    {
        m_consumer->close();
    }

    std::unique_ptr<RdKafka::Message> consume_one_message()
    {
        return std::unique_ptr<RdKafka::Message>(m_consumer->consume(10000));
    }

    int  consume_messages();
    int  try_consume_messages(int n_expected);
    void commit();

private:
    std::unique_ptr<RdKafka::KafkaConsumer> m_consumer;
    Logger                                  m_logger;
};

class Producer
{
public:
    Producer(TestConnections& test);
    bool produce_message(const std::string& topic, const std::string& key, const std::string& value);

    void flush()
    {
        m_producer->flush(10000);
    }

private:
    TestConnections&                   m_test;
    std::unique_ptr<RdKafka::Producer> m_producer;
    Logger                             m_logger;
};
