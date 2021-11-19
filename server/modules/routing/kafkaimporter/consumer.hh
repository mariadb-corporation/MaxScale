/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "config.hh"

#include <maxscale/ccdefs.hh>

#include <memory>
#include <string>
#include <vector>
#include <thread>

#include "producer.hh"

namespace kafkaimporter
{
class Consumer final
{
public:
    Consumer(const Config& config, Producer&& producer);
    ~Consumer();

private:
    void run();
    bool running() const;
    void stop();
    bool consume();
    bool commit();

    std::string offsets_to_string();

    const Config& m_config;
    Producer      m_producer;

    std::unique_ptr<RdKafka::KafkaConsumer> m_consumer;

    int64_t                   m_records {0};
    int64_t                   m_batch_size {0};
    mutable std::atomic<bool> m_running {true};
    std::thread               m_thread;
};
}
