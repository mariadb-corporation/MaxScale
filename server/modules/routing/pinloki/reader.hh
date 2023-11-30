/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
using AbortCallback = std::function<void ()>;

class Reader : public mxb::Worker::Callable
{
public:
    Reader(SendCallback cb,
           WorkerCallback worker_cb,
           AbortCallback abort_cb,
           const Config& conf,
           const maxsql::GtidList& start_gl,
           const std::chrono::seconds& heartbeat_interval);
    ~Reader();

    void start();

    void set_in_high_water(bool in_high_water);
    void send_events();

    std::weak_ptr<bool> get_ref()
    {
        return m_ref;
    }

private:
    static uint32_t epoll_update(class mxb::Pollable* data, mxb::Worker* worker, uint32_t events);
    void            start_reading();
    bool            poll_start_reading();
    void            notify_concrete_reader(uint32_t events);

    bool generate_heartbeats();

    struct Pollable : public mxb::Pollable
    {
        Pollable() = default;
        Pollable(Reader* reader, int fd);

        Reader* reader { nullptr };
        int     fd { -1 };

        int poll_fd() const override
        {
            return fd;
        }
        uint32_t handle_poll_events(mxb::Worker* worker, uint32_t events, Pollable::Context) override
        {
            return reader->epoll_update(this, worker, events);
        }
    };

    std::unique_ptr<FileReader> m_sFile_reader;

    SendCallback    m_send_callback;
    WorkerCallback  m_get_worker;
    AbortCallback   m_abort_cb;
    bool            m_in_high_water = false;
    InventoryReader m_inventory;
    Pollable        m_reader_poll_data;
    maxbase::Timer  m_timer {10s};

    // Related to delayed start
    maxsql::GtidList  m_start_gtid_list;
    mxb::Worker::DCId m_startup_poll_dcid = 0;

    // Heartbeat related variables
    mxb::Worker::DCId                     m_heartbeat_dcid = 0;
    std::chrono::seconds                  m_heartbeat_interval;
    std::chrono::steady_clock::time_point m_last_event;

    // Used to detect whether the session is still alive when callbacks are executed. This could be a
    // MXS_SESSION reference as well but the code is used in tests where this isn't easily available.
    //
    // TODO: replace with `lcall` once the code has been merged
    std::shared_ptr<bool> m_ref;
};
}
