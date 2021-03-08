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

#include <maxbase/exception.hh>
#include <maxbase/worker.hh>
#include <maxbase/stopwatch.hh>

#include "file_reader.hh"
#include "rpl_event.hh"

using namespace std::chrono_literals;

namespace pinloki
{

using Callback = std::function<bool (const maxsql::RplEvent&)>;

class Reader
{
public:
    Reader(Callback cb, const Config& conf, mxb::Worker* worker, const maxsql::GtidList& start_gl,
           const std::chrono::seconds& heartbeat_interval);
    ~Reader();

private:
    static uint32_t epoll_update(struct MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
    void            start_reading();
    bool            poll_start_reading(mxb::Worker::Call::action_t action);
    void            notify_concrete_reader(uint32_t events);
    void            handle_messages();

    bool resend_event(mxb::Worker::Call::action_t);
    bool generate_heartbeats(mxb::Worker::Call::action_t action);

    struct PollData : public MXB_POLL_DATA
    {
        PollData(Reader* reader, mxb::Worker* worker);
        Reader* reader;
    };

    std::unique_ptr<FileReader> m_sFile_reader;

    Callback        m_cb;
    InventoryReader m_inventory;
    PollData        m_reader_poll_data;
    mxb::Worker*    m_worker;
    uint32_t        m_dcid = 0;
    mxq::RplEvent   m_event;    // Stores the latest event that hasn't been processed
    maxbase::Timer  m_timer {10s};

    // Related to delayed start
    maxsql::GtidList m_start_gtid_list;
    uint32_t         m_startup_poll_dcid = 0;

    // Heartbeat related variables
    uint32_t                              m_heartbeat_dcid = 0;
    std::chrono::seconds                  m_heartbeat_interval;
    std::chrono::steady_clock::time_point m_last_event;
};
}
