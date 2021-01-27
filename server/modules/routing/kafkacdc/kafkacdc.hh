/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
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

// Never used
class KafkaCDCSession : public mxs::RouterSession
{
};

class KafkaCDC : public mxs::Router
{
public:
    KafkaCDC(const KafkaCDC&) = delete;
    KafkaCDC& operator=(const KafkaCDC&) = delete;

    struct Config
    {
        Config(const mxs::ConfigParameters& params)
            : bootstrap_servers(s_bootstrap_servers.get(params))
            , topic(s_topic.get(params))
            , enable_idempotence(s_enable_idempotence.get(params))
            , timeout(s_timeout.get(params))
            , gtid(s_gtid.get(params))
            , server_id(s_server_id.get(params))
            , cooperative_replication(s_cooperative_replication.get(params))
        {
        }

        std::string bootstrap_servers;
        std::string topic;
        bool        enable_idempotence;
        int         timeout;
        std::string gtid;
        int         server_id;
        bool        cooperative_replication;
    };

    ~KafkaCDC() = default;

    // Router capabilities
    static constexpr uint64_t CAPS = RCAP_TYPE_RUNTIME_CONFIG;

    static KafkaCDC* create(SERVICE* pService, mxs::ConfigParameters* params);

    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
    {
        return nullptr;
    }

    uint64_t getCapabilities() const
    {
        return CAPS;
    }

    json_t* diagnostics() const;
    bool    configure(mxs::ConfigParameters* param);

    mxs::config::Configuration* getConfiguration()
    {
        return nullptr;
    }

private:
    KafkaCDC(SERVICE* pService, Config&& config, std::unique_ptr<cdc::Replicator>&& rpl);

    static std::unique_ptr<cdc::Replicator> create_replicator(const Config& config, SERVICE* service);

    Config                           m_config;
    std::unique_ptr<cdc::Replicator> m_replicator;
    SERVICE*                         m_service;
};
