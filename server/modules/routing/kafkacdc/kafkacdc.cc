/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "kafkacdc.hh"

#include <maxbase/alloc.hh>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>

// RapidJSON uses std::iterator which has been deprecated
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace std::literals::string_view_literals;

namespace
{

namespace cfg = maxscale::config;
namespace rj = rapidjson;

constexpr const uint32_t PATH_FLAGS = cfg::ParamPath::C | cfg::ParamPath::W;

class KafkaSpecification : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:
    bool post_validate(const cfg::Configuration* config,
                       const mxs::ConfigParameters& params,
                       const std::map<std::string, mxs::ConfigParameters>& nested_params) const override;
    bool post_validate(const cfg::Configuration* config,
                       json_t* json,
                       const std::map<std::string, json_t*>& nested_params) const override;
};

KafkaSpecification s_spec(MXB_MODULE_NAME, cfg::Specification::ROUTER);

cfg::ParamString s_bootstrap_servers(
    &s_spec, "bootstrap_servers", "Bootstrap servers in host:port format",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_topic(
    &s_spec, "topic", "The topic where replicated events are sent",
    cfg::Param::AT_RUNTIME);

cfg::ParamBool s_enable_idempotence(
    &s_spec, "enable_idempotence", "Enables idempotent Kafka producer",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamCount s_timeout(
    &s_spec, "timeout", "Connection and read timeout for replication",
    10, cfg::Param::AT_RUNTIME);

cfg::ParamString s_gtid(
    &s_spec, "gtid", "The GTID position to start from",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamCount s_server_id(
    &s_spec, "server_id", "Server ID for direct replication mode",
    1234, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_cooperative_replication(
    &s_spec, "cooperative_replication", "Cooperate with other instances replicating from the same cluster",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_send_schema(
    &s_spec, "send_schema", "Add JSON schema events into the stream when table schema changes",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_match(
    &s_spec, "match", "Only include data from tables that match this pattern",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_exclude(
    &s_spec, "exclude", "Exclude data from tables that match this pattern",
    "", cfg::Param::AT_RUNTIME);

KafkaCommonConfig s_kafka(&s_spec);

bool KafkaSpecification::post_validate(const cfg::Configuration* config,
                                       const mxs::ConfigParameters& params,
                                       const std::map<std::string, mxs::ConfigParameters>& nested_params) const
{
    return s_kafka.post_validate(params);
}

bool KafkaSpecification::post_validate(const cfg::Configuration* config,
                                       json_t* json,
                                       const std::map<std::string, json_t*>& nested_params) const
{
    return s_kafka.post_validate(json);
}

std::string_view roweventtype_to_string(RowEvent type)
{
    switch (type)
    {
    case RowEvent::WRITE:
        return "insert"sv;

    case RowEvent::UPDATE:
        return "update_before"sv;

    case RowEvent::UPDATE_AFTER:
        return "update_after"sv;

    case RowEvent::DELETE:
        return "delete"sv;

    default:
        mxb_assert(!true);
        return "unknown"sv;
    }
}

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
                MXB_ERROR("Failed to create Kafka producer: %s", err.c_str());
            }
        }

        return rval;
    }

    gtid_pos_t load_latest_gtid() override
    {
        gtid_pos_t rval;

        if (auto cnf = create_config(m_config))
        {
            std::string err;
            cnf->set("group.id", "maxscale-kafkacdc", err);

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
                            MXB_INFO("Continuing replication from latest stored GTID in Kafka: %s",
                                     rval.to_string().c_str());
                        }
                        else
                        {
                            MXB_WARNING("Stored Kafka message does not contain a key, "
                                        "cannot restore position.");
                        }
                    }
                    else if (msg->err() != RdKafka::ERR_REQUEST_TIMED_OUT)
                    {
                        MXB_ERROR("Couldn't read GTID from Kafka: %s", msg->errstr().c_str());
                    }

                    delete msg;
                }
                else
                {
                    MXB_INFO("Kafka watermarks: High: %ld Low: %ld", high, low);
                }

                consumer->close();
                delete consumer;
            }
            else
            {
                MXB_ERROR("%s", err.c_str());
            }
        }

        return rval;
    }

    bool create_table(const Table& table) override
    {
        bool rval = true;

        if (m_config.send_schema && table_matches(table))
        {
            json_t* js = table.to_json();
            char* str = json_dumps(js, JSON_COMPACT);
            auto gtid = table.gtid.to_string();
            rval = produce(str, strlen(str), gtid.c_str(), gtid.length());
            free(str);
            json_decref(js);
        }

        return rval;
    }

    bool open_table(const Table& table) override
    {
        return true;
    }

    bool prepare_table(const Table& table) override
    {
        return true;
    }

    void flush_tables() override
    {
        m_producer->poll(0);
    }

    rj::Value as_str(std::string_view str)
    {
        return rj::Value(str.data(), str.size(), m_obj.GetAllocator());
    }

    void prepare_row(const Table& create,
                     const gtid_pos_t& gtid,
                     const REP_HEADER& hdr,
                     RowEvent event_type) override
    {
        m_match = table_matches(create);

        if (m_match)
        {
            m_obj.SetObject();
            auto type = roweventtype_to_string(event_type);

            // This uniquely identifies the event we're producing
            m_key = gtid.to_string() + ':' + std::to_string(gtid.event_num);
            auto& al = m_obj.GetAllocator();
            m_obj.AddMember("domain", gtid.domain, al);
            m_obj.AddMember("server_id", gtid.server_id, al);
            m_obj.AddMember("sequence", gtid.seq, al);
            m_obj.AddMember("event_number", gtid.event_num, al);
            m_obj.AddMember("timestamp", hdr.timestamp, al);
            m_obj.AddMember("event_type", as_str(type), al);
            m_obj.AddMember("table_schema", as_str(create.database), al);
            m_obj.AddMember("table_name", as_str(create.table), al);
        }
    }

    bool commit(const Table& create, const gtid_pos_t& gtid) override
    {
        bool rval = true;

        if (m_match)
        {
            rj::Writer<rj::StringBuffer> writer(m_buffer);
            m_obj.Accept(writer);
            rval = produce(m_buffer.GetString(), m_buffer.GetSize(), m_key.c_str(), m_key.length());
            m_obj.RemoveAllMembers();
            m_buffer.Clear();
        }

        return rval;
    }

    void column_int(const Table& create, int i, int32_t value) override
    {
        if (m_match)
        {
            auto key = as_str(create.columns[i].name);
            m_obj.AddMember(key, value, m_obj.GetAllocator());
        }
    }

    void column_long(const Table& create, int i, int64_t value) override
    {
        if (m_match)
        {
            auto key = as_str(create.columns[i].name);
            m_obj.AddMember(key, value, m_obj.GetAllocator());
        }
    }

    void column_float(const Table& create, int i, float value) override
    {
        if (m_match)
        {
            auto key = as_str(create.columns[i].name);
            m_obj.AddMember(key, value, m_obj.GetAllocator());
        }
    }

    void column_double(const Table& create, int i, double value) override
    {
        if (m_match)
        {
            auto key = as_str(create.columns[i].name);
            m_obj.AddMember(key, value, m_obj.GetAllocator());
        }
    }

    void column_string(const Table& create, int i, const std::string& value) override
    {
        if (m_match)
        {
            auto key = as_str(create.columns[i].name);
            auto val = as_str(value);
            m_obj.AddMember(key, val, m_obj.GetAllocator());
        }
    }

    void column_bytes(const Table& create, int i, uint8_t* value, int len) override
    {
        if (m_match)
        {
            auto key = as_str(create.columns[i].name);
            auto val = as_str(std::string_view((const char*)value, len));
            m_obj.AddMember(key, val, m_obj.GetAllocator());
        }
    }

    void column_null(const Table& create, int i) override
    {
        if (m_match)
        {
            auto key = as_str(create.columns[i].name);
            m_obj.AddMember(key, rj::Value(), m_obj.GetAllocator());
        }
    }

private:
    std::string             m_key;
    const KafkaCDC::Config& m_config;
    SProducer               m_producer;
    rj::Document            m_obj;
    rj::StringBuffer        m_buffer;
    bool                    m_match = false;
    int                     m_timeout = 10000;

    KafkaEventHandler(SProducer producer, const KafkaCDC::Config& config)
        : m_config(config)
        , m_producer(std::move(producer))
    {
    }

    /**
     * Produce a Kafka message
     *
     * @param data    Data to send to Kafka
     * @param datalen Length of the data
     * @param key     Key used to identify the message
     * @param keylen  Length of the key
     *
     * @return True if the message was queued successfully.
     */
    bool produce(const void* data, size_t datalen, const void* key, size_t keylen)
    {
        RdKafka::ErrorCode err;

        do
        {
            err = m_producer->produce(
                m_config.topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                (void*)data, datalen, key, keylen, 0, nullptr);

            if (err == RdKafka::ERR__QUEUE_FULL)
            {
                m_producer->poll(1000);
            }
            else if (err != RdKafka::ERR_NO_ERROR)
            {
                MXB_ERROR("%s", RdKafka::err2str(err).c_str());
                break;
            }
        }
        while (err == RdKafka::ERR__QUEUE_FULL);

        return err != RdKafka::ERR_NO_ERROR;
    }

    static std::unique_ptr<RdKafka::Conf> create_config(const KafkaCDC::Config& config)
    {
        // The configuration documentation for the connector:
        // https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md
        std::unordered_map<std::string, std::string> values;
        values["bootstrap.servers"] = config.bootstrap_servers;

        if (config.enable_idempotence)
        {
            values["enable.idempotence"] = "true";
            values["message.send.max.retries"] = "10000000";
        }

        if (config.ssl)
        {
            values["security.protocol"] = "ssl";
            values["ssl.ca.location"] = config.ssl_ca;
            values["ssl.certificate.location"] = config.ssl_cert;
            values["ssl.key.location"] = config.ssl_key;
        }

        if (!config.sasl_user.empty() && !config.sasl_password.empty())
        {
            values["security.protocol"] = config.ssl ? "sasl_ssl" : "sasl_plaintext";
            values["sasl.mechanism"] = to_string(config.sasl_mechanism);
            values["sasl.username"] = config.sasl_user;
            values["sasl.password"] = config.sasl_password;
        }

        return KafkaCommonConfig::create_config(values);
    }

    bool table_matches(const Table& create)
    {
        std::string identifier = create.id();
        return (m_config.match.empty() || m_config.match.match(identifier))
               && (m_config.exclude.empty() || !m_config.exclude.match(identifier));
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
    add_native(&Config::send_schema, &s_send_schema);
    add_native(&Config::match, &s_match);
    add_native(&Config::exclude, &s_exclude);
    add_native(&Config::ssl, &s_kafka.kafka_ssl);
    add_native(&Config::ssl_ca, &s_kafka.kafka_ssl_ca);
    add_native(&Config::ssl_cert, &s_kafka.kafka_ssl_cert);
    add_native(&Config::ssl_key, &s_kafka.kafka_ssl_key);
    add_native(&Config::sasl_user, &s_kafka.kafka_sasl_user);
    add_native(&Config::sasl_password, &s_kafka.kafka_sasl_password);
    add_native(&Config::sasl_mechanism, &s_kafka.kafka_sasl_mechanism);
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
    mxb::Json js(mxb::Json::Type::OBJECT);
    js.set_string("status", m_replicator->ok() ? "ok" : "error");
    js.set_string("gtid", m_replicator->gtid_pos());

    if (SERVER* target = m_replicator->target())
    {
        js.set_string("target", target->name());
    }
    else
    {
        js.set_null("target");
    }

    return js.release();
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
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
        &s_spec
    };

    return &info;
}
