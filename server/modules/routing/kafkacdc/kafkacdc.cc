/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-12-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "kafkacdc.hh"

#include <maxscale/paths.h>

#include <librdkafka/rdkafkacpp.h>

namespace
{

constexpr const char CN_BROKER[] = "broker";
constexpr const char CN_TOPIC[] = "topic";
constexpr const char CN_DATADIR[] = "datadir";

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

class KafkaEventHandler : public RowEventHandler
{
public:
    using SProducer = std::unique_ptr<RdKafka::Producer>;

    ~KafkaEventHandler()
    {
        // Wait in order to flush all events to Kafka (make this configurable?)
        m_producer->flush(60000);
    }

    static SRowEventHandler create(mxs::ConfigParameters* params)
    {
        SRowEventHandler rval;
        auto cnf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        auto broker = params->get_string(CN_BROKER);
        std::string err;

        if (cnf->set("bootstrap.servers", broker, err) == RdKafka::Conf::ConfResult::CONF_OK
            && cnf->set("enable.idempotence", "true", err) == RdKafka::Conf::ConfResult::CONF_OK
            && cnf->set("message.send.max.retries", "10000000", err) == RdKafka::Conf::ConfResult::CONF_OK
            && cnf->set("event_cb", new KafkaLogger, err) == RdKafka::Conf::ConfResult::CONF_OK)
        {
            if (auto producer = RdKafka::Producer::create(cnf, err))
            {
                rval.reset(new KafkaEventHandler(SProducer(producer), params->get_string(CN_TOPIC)));
            }
            else
            {
                MXS_ERROR("Failed to create Kafka producer: %s", err.c_str());
            }
        }
        else
        {
            MXS_ERROR("Failed to set Kafka parameters: %s", err.c_str());
        }

        delete cnf;
        return rval;
    }

    bool open_table(const Table& table)
    {
        json_t* js = table.to_json();
        bool rval = produce(js, nullptr, 0);
        json_decref(js);
        return rval;
    }

    bool prepare_table(const Table& table)
    {
        return true;
    }

    void flush_tables()
    {
        m_producer->poll(1000);
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
        json_object_set_new(m_obj, "gtid", json_string(gtid.to_string().c_str()));
        json_object_set_new(m_obj, "event_number", json_integer(gtid.event_num));
        json_object_set_new(m_obj, "event_type", json_string(type));
        json_object_set_new(m_obj, "timestamp", json_integer(hdr.timestamp));
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
    std::string m_key;
    std::string m_topic;
    SProducer   m_producer;
    json_t*     m_obj;

    KafkaEventHandler(SProducer producer, const std::string& topic)
        : m_topic(topic)
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
                m_topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_FREE,
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
};
}

KafkaCDC::KafkaCDC(SERVICE* pService, mxs::ConfigParameters* params)
    : Router<KafkaCDC, KafkaCDCSession>(pService)
{
    configure(params);
}

json_t* KafkaCDC::diagnostics() const
{
    mxb_assert(m_replicator);
    return json_pack("{s:s}", "status", m_replicator->ok() ? "ok" : "error");
}

bool KafkaCDC::configure(mxs::ConfigParameters* params)
{
    bool rval = false;

    if (auto handler = KafkaEventHandler::create(params))
    {
        cdc::Config cnf;
        cnf.service = m_pService;
        cnf.statedir = params->get_string(CN_DATADIR);

        // Resetting m_replicator before assigning the new values makes sure the old one stops
        // before the new one starts.
        m_replicator.reset();
        m_replicator = cdc::Replicator::start(cnf, std::move(handler));
        rval = true;
    }

    return rval;
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_ROUTER_VERSION,
        "Replicate data changes from MariaDB to Kafka",
        "V1.0.0",
        KafkaCDC::CAPS,
        &KafkaCDC::s_object,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {
            {
                CN_BROKER,
                MXS_MODULE_PARAM_STRING
            },
            {
                CN_TOPIC,
                MXS_MODULE_PARAM_STRING
            },
            {
                CN_DATADIR,
                MXS_MODULE_PARAM_PATH,
                MXS_DEFAULT_DATADIR,
                MXS_MODULE_OPT_PATH_R_OK
                | MXS_MODULE_OPT_PATH_W_OK
                | MXS_MODULE_OPT_PATH_X_OK
                | MXS_MODULE_OPT_PATH_CREAT
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
