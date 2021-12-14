/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "config.hh"

#include <maxscale/ccdefs.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>

#include "consumer.hh"

namespace kafkaimporter
{

class KafkaImporter : public mxs::Router, public PostConfigurable
{
public:
    KafkaImporter(const KafkaImporter&) = delete;
    KafkaImporter& operator=(const KafkaImporter&) = delete;

    ~KafkaImporter() = default;

    // Router capabilities
    static constexpr uint64_t CAPS = RCAP_TYPE_RUNTIME_CONFIG;

    static KafkaImporter* create(SERVICE* pService)
    {
        return new KafkaImporter(pService);
    }

    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override
    {
        return nullptr;
    }

    uint64_t getCapabilities() const override
    {
        return CAPS;
    }

    json_t* diagnostics() const override
    {
        return nullptr;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    bool post_configure() override final;

private:
    KafkaImporter(SERVICE* pService)
        : m_service(pService)
        , m_config(pService->name(), this)
    {
    }

    SERVICE*                  m_service;
    Config                    m_config;
    std::unique_ptr<Consumer> m_consumer;
};
}
