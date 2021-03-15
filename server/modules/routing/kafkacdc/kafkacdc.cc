/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "kafkacdc.hh"

#include <maxscale/paths.hh>

#include <librdkafka/rdkafkacpp.h>

namespace
{

const char* roweventtype_to_string(RowEvent type)
{
    switch (type)
    {
    case RowEvent::WRITE:
        return "insert";

    case RowEvent::UPDATE:
        return "update_before";

    case RowEvent::UPDATE_AFTER:
        return "update_after";

    case RowEvent::DELETE:
        return "delete";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

class KafkaLogger : public RdKafka::EventCb
{
public:
    void event_cb(RdKafka::Event& event) override
    {
        switch (event.type())
        {
        case RdKafka::Event::EVENT_LOG:
            MXB_LOG_MESSAGE(event.severity(), "%s", event.str().c_str());
            break;

        case RdKafka::Event::EVENT_ERROR:
            MXS_ERROR("%s", RdKafka::err2str(event.err()).c_str());
            break;

        default:
            MXS_INFO("%s", event.str().c_str());
            break;
        }
    }
};

static KafkaLogger kafka_logger;

class KafkaEventHandler : public RowEventHandler
{
public:
    using SProducer = std::unique_ptr<RdKafka::Producer>;

    ~KafkaEventHandler()
    {
        // Wait in order to flush all events to Kafka (make this configurable?)
        m_producer->flush(m_timeout);
    }

    static SRowEventHandler create(const KafkaCDC::Config& config)
    {
        std::string err;
        SRowEventHandler rval;

        if (auto cnf = create_config(config))
        {
            if (auto producer = RdKafka::Producer::create(cnf.get(), err))
            {
                rval.reset(new KafkaEventHandler(SProducer(producer), config));
            }
            else
            {
                MXS_ERROR("Failed to create Kafka producer: %s", err.c_str());
            }
        }

        return rval;
    }

    gtid_pos_t load_latest_gtid()
    {
        gtid_pos_t rval;

        if (auto cnf = create_config(m_config))
        {
            std::string err;

            if (auto consumer = RdKafka::KafkaConsumer::create(cnf.get(), err))
            {
                int64_t high = RdKafka::Topic::OFFSET_INVALID;
                int64_t low = RdKafka::Topic::OFFSET_INVALID;
                auto rc = consumer->query_watermark_offsets(m_config.topic, 0, &low, &high, m_timeout);

                if (high != RdKafka::Topic::OFFSET_INVALID && high > 0)
                {
                    std::vector<RdKafka::TopicPartition*> partitions;
                    partitions.push_back(RdKafka::TopicPartition::create(m_config.topic, 0, high - 1));
                    consumer->assign(partitions);
                    auto msg = consumer->consume(m_timeout);

                    for (auto p : partitions)
                    {
                        delete p;
                    }

                    if (msg->err() == RdKafka::ERR_NO_ERROR)
                    {
                        if (msg->key())
                        {
                            rval = gtid_pos_t::from_string(*msg->key());
                            MXS_INFO("Continuing replication from latest stored GTID in Kafka: %s",
                                     rval.to_string().c_str());
                        }
                        else
                        {
                            MXS_WARNING("Stored Kafka message does not contain a key, "
                                        "cannot restore position.");
                        }
                    }
                    else if (msg->err() != RdKafka::ERR_REQUEST_TIMED_OUT)
                    {
                        MXS_ERROR("Couldn't read GTID from Kafka: %s", msg->errstr().c_str());
                    }

                    delete msg;
                }
                else
                {
                    MXS_INFO("Kafka watermarks: High: %ld Low: %ld", high, low);
                }

                consumer->close();
                delete consumer;
            }
            else
            {
                MXS_ERROR("%s", err.c_str());
            }
        }

        return rval;
    }

    bool create_table(const Table& table)
    {
        json_t* js = table.to_json();
        auto gtid = table.gtid.to_string();
        bool rval = produce(js, gtid.c_str(), gtid.length());
        json_decref(js);
        return rval;
    }

    bool open_table(const Table& table)
    {
        return true;
    }

    bool prepare_table(const Table& table)
    {
        return true;
    }

    void flush_tables()
    {
        m_producer->poll(0);
    }

    void prepare_row(const Table& create,
                     const gtid_pos_t& gtid,
                     const REP_HEADER& hdr,
                     RowEvent event_type)
    {
        auto type = roweventtype_to_string(event_type);

        // This uniquely identifies the event we're producing
        m_key = gtid.to_string() + ':' + std::to_string(gtid.event_num);

        m_obj = json_object();
        json_object_set_new(m_obj, "domain", json_integer(gtid.domain));
        json_object_set_new(m_obj, "server_id", json_integer(gtid.server_id));
        json_object_set_new(m_obj, "sequence", json_integer(gtid.seq));
        json_object_set_new(m_obj, "event_number", json_integer(gtid.event_num));
        json_object_set_new(m_obj, "timestamp", json_integer(hdr.timestamp));
        json_object_set_new(m_obj, "event_type", json_string(type));
        json_object_set_new(m_obj, "table_schema", json_string(create.database.c_str()));
        json_object_set_new(m_obj, "table_name", json_string(create.table.c_str()));
    }

    bool commit(const Table& create, const gtid_pos_t& gtid)
    {
        bool rval = produce(m_obj, m_key.c_str(), m_key.length());
        json_decref(m_obj);
        m_obj = nullptr;
        return rval;
    }

    void column_int(const Table& create, int i, int32_t value)
    {
        json_object_set_new(m_obj, create.columns[i].name.c_str(), json_integer(value));
    }

    void column_long(const Table& create, int i, int64_t value)
    {
        json_object_set_new(m_obj, create.columns[i].name.c_str(), json_integer(value));
    }

    void column_float(const Table& create, int i, float value)
    {
        json_object_set_new(m_obj, create.columns[i].name.c_str(), json_real(value));
    }

    void column_double(const Table& create, int i, double value)
    {
        json_object_set_new(m_obj, create.columns[i].name.c_str(), json_real(value));
    }

    void column_string(const Table& create, int i, const std::string& value)
    {
        json_object_set_new(m_obj, create.columns[i].name.c_str(), json_string(value.c_str()));
    }

    void column_bytes(const Table& create, int i, uint8_t* value, int len)
    {
        json_object_set_new(m_obj, create.columns[i].name.c_str(),
                            json_stringn_nocheck((const char*)value, len));
    }

    void column_null(const Table& create, int i)
    {
        json_object_set_new(m_obj, create.columns[i].name.c_str(), json_null());
    }

private:
    std::string             m_key;
    const KafkaCDC::Config& m_config;
    SProducer               m_producer;
    json_t*                 m_obj;
    int                     m_timeout = 10000;

    KafkaEventHandler(SProducer producer, const KafkaCDC::Config& config)
        : m_config(config)
        , m_producer(std::move(producer))
    {
    }

    /**
     * Produce a Kafka message
     *
     * @param obj    JSON object to send to Kafka
     * @param key    Key used to identify the message
     * @param keylen Length of the key
     *
     * @return True if the message was queued successfully.
     */
    bool produce(json_t* obj, const void* key, size_t keylen)
    {
        char* json = json_dumps(obj, JSON_COMPACT);

        RdKafka::ErrorCode err;

        do
        {
            err = m_producer->produce(
                m_config.topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_FREE,
                json, strlen(json), key, keylen, 0, nullptr);

            if (err == RdKafka::ERR__QUEUE_FULL)
            {
                m_producer->poll(1000);
            }
            else if (err != RdKafka::ERR_NO_ERROR)
            {
                MXS_ERROR("%s", RdKafka::err2str(err).c_str());
                MXS_FREE(json);
                break;
            }
        }
        while (err == RdKafka::ERR__QUEUE_FULL);

        return err != RdKafka::ERR_NO_ERROR;
    }

    static std::unique_ptr<RdKafka::Conf> create_config(const KafkaCDC::Config& config)
    {
        constexpr const auto OK = RdKafka::Conf::ConfResult::CONF_OK;
        std::string err;
        std::unique_ptr<RdKafka::Conf> cnf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

        if (cnf)
        {
            if (cnf->set("event_cb", &kafka_logger, err) != OK)
            {
                MXS_ERROR("Failed to set Kafka event logger: %s", err.c_str());
                cnf.reset();
            }
            else if (cnf->set("bootstrap.servers", config.bootstrap_servers, err) != OK)
            {
                MXS_ERROR("Failed to set `bootstrap.servers`: %s", err.c_str());
                cnf.reset();
            }
            else if (cnf->set("group.id", "maxscale-kafkacdc", err) != OK)
            {
                MXS_ERROR("Failed to set `group.id`: %s", err.c_str());
                cnf.reset();
            }
            else if (config.enable_idempotence
                     && (cnf->set("enable.idempotence", "true", err) != OK
                         || cnf->set("message.send.max.retries", "10000000", err) != OK))
            {
                MXS_ERROR("Failed to enable idempotent producer: %s", err.c_str());
                cnf.reset();
            }
            else if (config.ssl)
            {
                if (cnf->set("security.protocol", "ssl", err) != OK)
                {
                    MXS_ERROR("Failed to set `security.protocol`: %s", err.c_str());
                    cnf.reset();
                }
                else if (!config.ssl_ca.empty()
                         && cnf->set("ssl.ca.location", config.ssl_ca, err) != OK)
                {
                    MXS_ERROR("Failed to set CA certificate: %s", err.c_str());
                    cnf.reset();
                }
                else if (!config.ssl_cert.empty()
                         && cnf->set("ssl.certificate.location", config.ssl_cert, err) != OK)
                {
                    MXS_ERROR("Failed to set public certificate: %s", err.c_str());
                    cnf.reset();
                }
                else if (!config.ssl_key.empty()
                         && cnf->set("ssl.key.location", config.ssl_key, err) != OK)
                {
                    MXS_ERROR("Failed to set private key: %s", err.c_str());
                    cnf.reset();
                }
            }
        }

        return cnf;
    }
};
}

KafkaCDC::Config::Config(const std::string& name, KafkaCDC* router)
    : mxs::config::Configuration(name, &s_spec)
    , m_router(router)
{
    add_native(&Config::bootstrap_servers, &s_bootstrap_servers);
    add_native(&Config::topic, &s_topic);
    add_native(&Config::enable_idempotence, &s_enable_idempotence);
    add_native(&Config::timeout, &s_timeout);
    add_native(&Config::gtid, &s_gtid);
    add_native(&Config::server_id, &s_server_id);
    add_native(&Config::cooperative_replication, &s_cooperative_replication);
    add_native(&Config::ssl, &s_kafka_ssl);
    add_native(&Config::ssl_ca, &s_kafka_ssl_ca);
    add_native(&Config::ssl_cert, &s_kafka_ssl_cert);
    add_native(&Config::ssl_key, &s_kafka_ssl_key);
}

bool KafkaCDC::Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    return m_router->post_configure();
}

// static
KafkaCDC* KafkaCDC::create(SERVICE* pService)
{
    return new KafkaCDC(pService);
}

bool KafkaCDC::post_configure()
{
    // Resetting m_replicator before assigning the new values makes sure the old one stops
    // before the new one starts.
    m_replicator.reset();
    m_replicator = create_replicator(m_config, m_service);
    return m_replicator.get();
}

// static
std::unique_ptr<cdc::Replicator> KafkaCDC::create_replicator(const Config& config, SERVICE* service)
{
    std::unique_ptr<cdc::Replicator> rval;
    if (auto handler = KafkaEventHandler::create(config))
    {
        cdc::Config cnf;
        cnf.service = service;
        cnf.statedir = std::string(mxs::datadir()) + "/" + service->name();
        cnf.timeout = config.timeout;
        cnf.gtid = config.gtid;
        cnf.server_id = config.server_id;
        cnf.cooperate = config.cooperative_replication;

        // Make sure the data directory exists
        mxs_mkdir_all(cnf.statedir.c_str(), 0755);

        // Resetting m_replicator before assigning the new values makes sure the old one stops
        // before the new one starts.
        rval = cdc::Replicator::start(cnf, std::move(handler));
    }

    return rval;
}

KafkaCDC::KafkaCDC(SERVICE* pService)
    : m_config(pService->name(), this)
    , m_service(pService)
{
}

json_t* KafkaCDC::diagnostics() const
{
    mxb_assert(m_replicator);
    return json_pack("{s:s}", "status", m_replicator->ok() ? "ok" : "error");
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::ALPHA,
        MXS_ROUTER_VERSION,
        "Replicate data changes from MariaDB to Kafka",
        "V1.0.0",
        KafkaCDC::CAPS,
        &mxs::RouterApi<KafkaCDC>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        &s_spec
    };

    return &info;
}
