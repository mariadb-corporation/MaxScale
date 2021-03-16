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
#pragma once

#define MXS_MODULE_NAME "kafkacdc"

#include <maxscale/ccdefs.hh>
#include <maxscale/router.hh>
#include <maxscale/config2.hh>
#include <maxscale/paths.hh>

#include "../replicator/replicator.hh"

namespace cfg = maxscale::config;

constexpr const uint32_t PATH_FLAGS = cfg::ParamPath::C | cfg::ParamPath::W;

static cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::ROUTER);

static cfg::ParamString s_bootstrap_servers(
    &s_spec, "bootstrap_servers", "Bootstrap servers in host:port format");

static cfg::ParamString s_topic(
    &s_spec, "topic", "The topic where replicated events are sent");

static cfg::ParamBool s_enable_idempotence(
    &s_spec, "enable_idempotence", "Enables idempotent Kafka producer", false);

static cfg::ParamCount s_timeout(
    &s_spec, "timeout", "Connection and read timeout for replication", 10);

static cfg::ParamString s_gtid(
    &s_spec, "gtid", "The GTID position to start from", "");

static cfg::ParamCount s_server_id(
    &s_spec, "server_id", "Server ID for direct replication mode", 1234);

static cfg::ParamBool s_cooperative_replication(
    &s_spec, "cooperative_replication", "Cooperate with other instances replicating from the same cluster",
    false);

static cfg::ParamBool s_kafka_ssl(
    &s_spec, "kafka_ssl", "Enable SSL for Kafka connections",
    false);

static cfg::ParamPath s_kafka_ssl_ca(
    &s_spec, "kafka_ssl_ca", "SSL Certificate Authority file in PEM format",
    cfg::ParamPath::R, "");

static cfg::ParamPath s_kafka_ssl_cert(
    &s_spec, "kafka_ssl_cert", "SSL public certificate file in PEM format",
    cfg::ParamPath::R, "");

static cfg::ParamPath s_kafka_ssl_key(
    &s_spec, "kafka_ssl_key", "SSL private key file in PEM format",
    cfg::ParamPath::R, "");

static cfg::ParamString s_kafka_sasl_user(
    &s_spec, "kafka_sasl_user", "SASL username used for authentication",
    "");

static cfg::ParamString s_kafka_sasl_password(
    &s_spec, "kafka_sasl_password", "SASL password for the user",
    "");

enum SaslMech
{
    PLAIN,
    SCRAM_SHA_256,
    SCRAM_SHA_512,
};

static cfg::ParamEnum<SaslMech> s_kafka_sasl_mechanism(
    &s_spec, "kafka_sasl_mechanism", "SASL mechanism to use",
{
    {PLAIN, "PLAIN"},
    {SCRAM_SHA_256, "SCRAM-SHA-256"},
    {SCRAM_SHA_512, "SCRAM-SHA-512"},
},
    PLAIN);

// Never used
class KafkaCDCSession : public mxs::RouterSession
{
};

class KafkaCDC : public mxs::Router
{
public:
    KafkaCDC(const KafkaCDC&) = delete;
    KafkaCDC& operator=(const KafkaCDC&) = delete;

    class Config : public mxs::config::Configuration
    {
    public:
        Config(const std::string& name, KafkaCDC* router);

        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params);

        std::string bootstrap_servers;
        std::string topic;
        bool        enable_idempotence;
        int64_t     timeout;
        std::string gtid;
        int64_t     server_id;
        bool        cooperative_replication;

        bool        ssl;
        std::string ssl_ca;
        std::string ssl_key;
        std::string ssl_cert;

        std::string sasl_user;
        std::string sasl_password;
        SaslMech    sasl_mechanism;

    private:
        KafkaCDC* m_router;
    };

    ~KafkaCDC() = default;

    // Router capabilities
    static constexpr uint64_t CAPS = RCAP_TYPE_RUNTIME_CONFIG;

    static KafkaCDC* create(SERVICE* pService);

    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
    {
        return nullptr;
    }

    uint64_t getCapabilities() const
    {
        return CAPS;
    }

    json_t* diagnostics() const;

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

    bool post_configure();

private:
    KafkaCDC(SERVICE* pService);

    static std::unique_ptr<cdc::Replicator> create_replicator(const Config& config, SERVICE* service);

    Config                           m_config;
    std::unique_ptr<cdc::Replicator> m_replicator;
    SERVICE*                         m_service;
};
