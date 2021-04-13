/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "config.hh"

namespace
{

namespace cfg = maxscale::config;
using namespace kafkaconsumer;

class KafkaSpecification : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:
    bool post_validate(const mxs::ConfigParameters& params) const;
    bool post_validate(json_t* json) const;
};

KafkaSpecification s_spec(MXS_MODULE_NAME, cfg::Specification::ROUTER);

cfg::ParamString s_bootstrap_servers(
    &s_spec, "bootstrap_servers", "Kafka bootstrap servers in host:port format",
    cfg::Param::AT_RUNTIME);

cfg::ParamStringList s_topics(
    &s_spec, "topics", "The comma separated list of topics to subscribe to",
    ",", cfg::Param::AT_RUNTIME);

cfg::ParamCount s_batch_size(
    &s_spec, "batch_size", "Maximum number of uncommitted records",
    100, cfg::Param::AT_RUNTIME);

cfg::ParamEnum<IDType> s_table_name_in(
    &s_spec, "table_name_in",
    "What is used to locate which table to insert the data into (topic name or record key)",
    {
        {ID_FROM_TOPIC, "topic"},
        {ID_FROM_KEY, "key"},
    }, ID_FROM_TOPIC, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_timeout(
    &s_spec, "timeout", "Connection and read timeout for network communication",
    cfg::INTERPRET_AS_SECONDS, std::chrono::seconds(5), cfg::Param::AT_RUNTIME);

KafkaCommonConfig s_kafka(&s_spec);

bool KafkaSpecification::post_validate(const mxs::ConfigParameters& params) const
{
    return s_kafka.post_validate(params);
}

bool KafkaSpecification::post_validate(json_t* json) const
{
    return s_kafka.post_validate(json);
}
}

namespace kafkaconsumer
{

Config::Config(const std::string& name, PostConfigurable* router)
    : mxs::config::Configuration(name, &s_spec)
    , bootstrap_servers(this, &s_bootstrap_servers)
    , topics(this, &s_topics)
    , batch_size(this, &s_batch_size)
    , table_name_in(this, &s_table_name_in)
    , timeout(this, &s_timeout)
    , ssl(this, &s_kafka.kafka_ssl)
    , ssl_ca(this, &s_kafka.kafka_ssl_ca)
    , ssl_cert(this, &s_kafka.kafka_ssl_cert)
    , ssl_key(this, &s_kafka.kafka_ssl_key)
    , sasl_user(this, &s_kafka.kafka_sasl_user)
    , sasl_password(this, &s_kafka.kafka_sasl_password)
    , sasl_mechanism(this, &s_kafka.kafka_sasl_mechanism)
    , m_router(router)
{
}

bool Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    return m_router->post_configure();
}

// static
mxs::config::Specification* Config::specification()
{
    return &s_spec;
}
}
