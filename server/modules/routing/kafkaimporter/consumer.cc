#include "consumer.hh"

using std::chrono::duration_cast;
using std::chrono::milliseconds;

namespace
{
using namespace kafkaimporter;

std::unique_ptr<RdKafka::Conf> create_config(const Config& config)
{
    // The configuration documentation for the connector:
    // https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md
    std::unordered_map<std::string, std::string> values;
    values["bootstrap.servers"] = config.bootstrap_servers.get();
    values["group.id"] = "maxscale-KafkaImporter";
    values["enable.auto.commit"] = "false";
    values["enable.auto.offset.store"] = "true";
    values["auto.offset.reset"] = "smallest";
    values["allow.auto.create.topics"] = "true";
    values["topic.metadata.refresh.interval.ms"] = "10000";

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

namespace kafkaimporter
{

Consumer::Consumer(const Config& config, Producer&& producer)
    : m_config(config)
    , m_producer(std::move(producer))
    , m_batch_size(config.batch_size.get())
{
    m_thread = std::thread(&Consumer::run, this);
}

Consumer::~Consumer()
{
    stop();
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
            std::this_thread::sleep_for(m_config.timeout.get());
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
        int timeout = duration_cast<milliseconds>(m_config.timeout.get()).count();
        bool use_key = m_config.table_name_in.get() == ID_FROM_KEY;

        m_consumer.reset(RdKafka::KafkaConsumer::create(cnf.get(), err));

        if (m_consumer)
        {
            m_consumer->subscribe(m_config.topics.get());

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
                else if (msg->err() == RdKafka::ERR_UNKNOWN_TOPIC_OR_PART)
                {
                    ok = false;
                    MXS_WARNING("%s", msg->errstr().c_str());
                    break;
                }
                else
                {
                    ok = false;
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
