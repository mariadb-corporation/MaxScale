/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "kafkaimporter.hh"

namespace kafkaimporter
{

bool KafkaImporter::post_configure()
{
    // Reset the consumer before we start a new one to make sure there's only one of them running
    m_consumer.reset();
    m_consumer = std::make_unique<Consumer>(m_config, Producer(m_config, m_service));
    return true;
}
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::GA,
        MXS_ROUTER_VERSION,
        "Stream Kafka messages into MariaDB",
        "1.0.0",
        kafkaimporter::KafkaImporter::CAPS,
        &mxs::RouterApi<kafkaimporter::KafkaImporter>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        kafkaimporter::Config::specification()
    };

    return &info;
}
