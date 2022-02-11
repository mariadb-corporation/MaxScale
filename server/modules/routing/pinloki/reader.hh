/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/exception.hh>
#include <maxbase/worker.hh>
#include <maxbase/stopwatch.hh>

#include "file_reader.hh"
#include "rpl_event.hh"

using namespace std::chrono_literals;

namespace pinloki
{

using SendCallback = std::function<void (const maxsql::RplEvent&)>;
using WorkerCallback = std::function<mxb::Worker& ()>;

class Reader
{
public:
    Reader(SendCallback cb,
           WorkerCallback worker_cb,
           const Config& conf,
           const maxsql::GtidList& start_gl,
           const std::chrono::seconds& heartbeat_interval);
    ~Reader();

    void start();

    void set_in_high_water(bool in_high_water);
    void send_events();

private:
    static uint32_t epoll_update(struct MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
    void            start_reading();
    bool            poll_start_reading(mxb::Worker::Call::action_t action);
    void            notify_concrete_reader(uint32_t events);

    bool generate_heartbeats(mxb::Worker::Call::action_t action);

    struct PollData : public MXB_POLL_DATA
    {
        PollData() = default;
        PollData(Reader* reader, mxb::Worker* worker);
        Reader* reader;
    };

    std::unique_ptr<FileReader> m_sFile_reader;

    SendCallback    m_send_callback;
    WorkerCallback  m_get_worker;
    bool            m_in_high_water = false;
    InventoryReader m_inventory;
    PollData        m_reader_poll_data;
    maxbase::Timer  m_timer {10s};

    // Related to delayed start
    maxsql::GtidList  m_start_gtid_list;
    mxb::Worker::DCId m_startup_poll_dcid = 0;

    // Heartbeat related variables
    mxb::Worker::DCId                     m_heartbeat_dcid = 0;
    std::chrono::seconds                  m_heartbeat_interval;
    std::chrono::steady_clock::time_point m_last_event;
};
}
