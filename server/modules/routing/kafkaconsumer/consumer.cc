#include "consumer.hh"

namespace
{
using namespace kafkaconsumer;

std::unique_ptr<RdKafka::Conf> create_config(const Config& config)
{
    // The configuration documentation for the connector:
    // https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md
    std::unordered_map<std::string, std::string> values;
    values["bootstrap.servers"] = config.bootstrap_servers.get();
    values["group.id"] = "maxscale-KafkaConsumer";
    values["enable.auto.commit"] = "false";
    values["enable.auto.offset.store"] = "true";

    if (config.ssl.get())
    {
        values["security.protocol"] = "ssl";
        values["ssl.ca.location"] = config.ssl_ca.get();
        values["ssl.certificate.location"] = config.ssl_cert.get();
        values["ssl.key.location"] = config.ssl_key.get();
    }

    if (!config.sasl_user.get().empty() && !config.sasl_password.get().empty())
    {
        values["security.protocol"] = config.ssl.get() ? "sasl_ssl" : "sasl_plaintext";
        values["sasl.mechanism"] = to_string(config.sasl_mechanism.get());
        values["sasl.username"] = config.sasl_user.get();
        values["sasl.password"] = config.sasl_password.get();
    }

    return KafkaCommonConfig::create_config(values);
}
}

namespace kafkaconsumer
{

Consumer::Consumer(const Config& config, Producer&& producer)
    : m_config(config)
    , m_producer(std::move(producer))
    , m_batch_size(config.batch_size.get())
{
    for (auto t : config.topics.get())
    {
        m_partitions.push_back(RdKafka::TopicPartition::create(t, 0));
    }

    m_thread = std::thread(&Consumer::run, this);
}

Consumer::~Consumer()
{
    stop();

    for (auto p : m_partitions)
    {
        delete p;
    }
}

bool Consumer::running() const
{
    return m_running.load(std::memory_order_relaxed);
}

void Consumer::run()
{
    while (running())
    {
        if (!consume())
        {
            // Something went wrong, sleep for a while before trying again.
            for (int i = 0; i < 10 && running(); i++)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
}

void Consumer::stop()
{
    m_running.store(false, std::memory_order_relaxed);
    m_thread.join();
}

bool Consumer::commit()
{
    bool ok = true;

    if (m_records > 0)
    {
        if (m_producer.flush())
        {
            auto err = m_consumer->commitSync();

            if (err == RdKafka::ERR_NO_ERROR)
            {
                m_records = 0;
                MXS_INFO("Offsets committed: %s", offsets_to_string().c_str());
            }
            else
            {
                MXS_ERROR("Failed to commit offsets: %s", RdKafka::err2str(err).c_str());
                ok = false;
            }
        }
        else
        {
            ok = false;
        }
    }

    return ok;
}

std::string Consumer::offsets_to_string()
{
    std::string rval;
    const char* sep = "";

    std::vector<RdKafka::TopicPartition*> part;
    m_consumer->assignment(part);
    m_consumer->position(part);

    for (const auto* p : part)
    {
        std::string offset;
        int64_t os = p->offset();

        if (os == RdKafka::Topic::OFFSET_INVALID)
        {
            offset = "NO_OFFSET";
        }
        else
        {
            offset = std::to_string(os);
        }

        rval += sep + p->topic() + ": " + offset;
        sep = ", ";
    }

    RdKafka::TopicPartition::destroy(part);

    return "[" + rval + "]";
}

bool Consumer::consume()
{
    bool ok = true;
    // Reset the record in case the previous run failed to commit some records
    m_records = 0;

    if (auto cnf = create_config(m_config))
    {
        std::string err;
        int timeout = 1000;
        bool use_key = m_config.table_name_in.get() == ID_FROM_KEY;

        m_consumer.reset(RdKafka::KafkaConsumer::create(cnf.get(), err));

        if (m_consumer)
        {
            auto kafka_err = m_consumer->committed(m_partitions, timeout * 10);

            if (kafka_err != RdKafka::ERR_NO_ERROR)
            {
                MXS_ERROR("Failed fetch committed offsets: %s", RdKafka::err2str(kafka_err).c_str());
                return false;
            }

            m_consumer->assign(m_partitions);

            MXS_INFO("Starting from committed offsets: %s", offsets_to_string().c_str());

            while (running())
            {
                std::unique_ptr<RdKafka::Message> msg(m_consumer->consume(timeout));

                if (msg->err() == RdKafka::ERR_NO_ERROR)
                {
                    std::string value((const char*)msg->payload(), msg->len());
                    std::string key;

                    if (msg->key())
                    {
                        key = *msg->key();
                    }
                    else if (use_key)
                    {
                        MXS_INFO("Ignoring record at offset %ld, no record key provided.", msg->offset());
                        continue;
                    }

                    if (!m_producer.produce(use_key ? key : msg->topic_name(), value))
                    {
                        break;
                    }

                    ++m_records;

                    if (m_records >= m_batch_size)
                    {
                        if (!commit())
                        {
                            ok = false;
                            break;
                        }
                    }
                }
                else if (msg->err() == RdKafka::ERR_REQUEST_TIMED_OUT
                         || msg->err() == RdKafka::ERR__TIMED_OUT)
                {
                    m_consumer->poll(0);

                    if (!commit())
                    {
                        ok = false;
                        break;
                    }
                }
                else
                {
                    MXS_ERROR("%s", msg->errstr().c_str());
                    break;
                }
            }

            if (ok)
            {
                // Controlled shutdown, try to commit pending records.
                commit();
            }

            m_consumer->close();
        }
        else
        {
            MXS_ERROR("Failed to create consumer: %s", err.c_str());
            ok = false;
        }
    }
    else
    {
        ok = false;
    }

    return ok;
}
}
