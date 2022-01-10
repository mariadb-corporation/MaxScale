/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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
#include "kafka_common.hh"

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

        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

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

    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override
    {
        return nullptr;
    }

    uint64_t getCapabilities() const override
    {
        return CAPS;
    }

    json_t* diagnostics() const override;

    mxs::config::Configuration& getConfiguration() override
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
