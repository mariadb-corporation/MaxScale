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
#pragma once

#include "config.hh"

#include <maxscale/ccdefs.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>

#include "consumer.hh"

namespace kafkaconsumer
{

class KafkaConsumer : public mxs::Router, public PostConfigurable
{
public:
    KafkaConsumer(const KafkaConsumer&) = delete;
    KafkaConsumer& operator=(const KafkaConsumer&) = delete;

    ~KafkaConsumer() = default;

    // Router capabilities
    static constexpr uint64_t CAPS = RCAP_TYPE_RUNTIME_CONFIG;

    static KafkaConsumer* create(SERVICE* pService)
    {
        return new KafkaConsumer(pService);
    }

    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
    {
        return nullptr;
    }

    uint64_t getCapabilities() const
    {
        return CAPS;
    }

    json_t* diagnostics() const
    {
        return nullptr;
    }

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

    bool post_configure() final;

private:
    KafkaConsumer(SERVICE* pService)
        : m_service(pService)
        , m_config(pService->name(), this)
    {
    }

    SERVICE*                  m_service;
    Config                    m_config;
    std::unique_ptr<Consumer> m_consumer;
};
}
