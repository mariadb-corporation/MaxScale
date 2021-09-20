/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/assert.h>
#include <maxscale/config2.hh>

#include <librdkafka/rdkafkacpp.h>

#include <unordered_map>

enum SaslMech
{
    PLAIN,
    SCRAM_SHA_256,
    SCRAM_SHA_512,
};

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

struct KafkaCommonConfig
{
    KafkaCommonConfig(mxs::config::Specification* spec)
        : kafka_ssl(
            spec, "kafka_ssl", "Enable SSL for Kafka connections",
            false, mxs::config::Param::AT_RUNTIME)
        , kafka_ssl_ca(
            spec, "kafka_ssl_ca", "SSL Certificate Authority file in PEM format",
            mxs::config::ParamPath::R, "", mxs::config::Param::AT_RUNTIME)
        , kafka_ssl_cert(
            spec, "kafka_ssl_cert", "SSL public certificate file in PEM format",
            mxs::config::ParamPath::R, "", mxs::config::Param::AT_RUNTIME)
        , kafka_ssl_key(
            spec, "kafka_ssl_key", "SSL private key file in PEM format",
            mxs::config::ParamPath::R, "", mxs::config::Param::AT_RUNTIME)
        , kafka_sasl_user(
            spec, "kafka_sasl_user", "SASL username used for authentication",
            "", mxs::config::Param::AT_RUNTIME)
        , kafka_sasl_password(
            spec, "kafka_sasl_password", "SASL password for the user",
            "", mxs::config::Param::AT_RUNTIME)
        , kafka_sasl_mechanism(
            spec, "kafka_sasl_mechanism", "SASL mechanism to use",
    {
        {PLAIN, "PLAIN"},
        {SCRAM_SHA_256, "SCRAM-SHA-256"},
        {SCRAM_SHA_512, "SCRAM-SHA-512"},
    },
            PLAIN, mxs::config::Param::AT_RUNTIME)
    {
    }

    template<class Param>
    bool post_validate(Param param)
    {
        bool ok = true;

        if (kafka_ssl_key.get(param).empty() != kafka_ssl_cert.get(param).empty())
        {
            MXS_ERROR("Both '%s' and '%s' must be defined",
                      kafka_ssl_key.name().c_str(),
                      kafka_ssl_cert.name().c_str());
            ok = false;
        }

        if (kafka_sasl_user.get(param).empty() != kafka_sasl_password.get(param).empty())
        {
            MXS_ERROR("Both '%s' and '%s' must be defined",
                      kafka_sasl_user.name().c_str(),
                      kafka_sasl_password.name().c_str());
            ok = false;
        }

        return ok;
    }

    static std::unique_ptr<RdKafka::Conf>
    create_config(const std::unordered_map<std::string, std::string>& values)
    {
        const auto OK = RdKafka::Conf::ConfResult::CONF_OK;
        std::string err;
        std::unique_ptr<RdKafka::Conf> cnf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

        for (const auto& kv : values)
        {
            if (!kv.second.empty() && cnf->set(kv.first, kv.second, err) != OK)
            {
                MXS_ERROR("Failed to set `%s`: %s", kv.first.c_str(), err.c_str());
                cnf.reset();
                break;
            }
        }

        static KafkaLogger kafka_logger;

        if (cnf && cnf->set("event_cb", &kafka_logger, err) != OK)
        {
            MXS_ERROR("Failed to set Kafka event logger: %s", err.c_str());
            cnf.reset();
        }

        return cnf;
    }

    mxs::config::ParamBool           kafka_ssl;
    mxs::config::ParamPath           kafka_ssl_ca;
    mxs::config::ParamPath           kafka_ssl_cert;
    mxs::config::ParamPath           kafka_ssl_key;
    mxs::config::ParamString         kafka_sasl_user;
    mxs::config::ParamString         kafka_sasl_password;
    mxs::config::ParamEnum<SaslMech> kafka_sasl_mechanism;
};

static inline std::string to_string(SaslMech mech)
{
    switch (mech)
    {
    case PLAIN:
        return "PLAIN";

    case SCRAM_SHA_256:
        return "SCRAM-SHA-256";

    case SCRAM_SHA_512:
        return "SCRAM-SHA-512";
    }

    mxb_assert(!true);
    return "";
}
