/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <memory>
#include <mutex>
#include <vector>

#include <maxscale/utils.h>
#include <maxscale/mainworker.hh>

#include "internal/websocket.hh"

namespace
{
#ifdef EPOLLRDHUP
static constexpr uint32_t EVENTS = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
#else
static constexpr uint32_t EVENTS = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLET;
#endif

struct ThisUnit
{
    std::mutex                              lock;
    std::vector<std::unique_ptr<WebSocket>> connections;
} this_unit;
}

// static
void WebSocket::create(int fd, MHD_UpgradeResponseHandle* urh, std::function<std::string ()> cb)
{
    auto worker = mxs::MainWorker::get();
    std::unique_ptr<WebSocket> ws(new WebSocket(fd, urh, cb));

    // Send the initial payload and then add it to the worker to see when the socket drains
    if (ws->send() && worker->add_fd(fd, EVENTS, ws.get()))
    {
        worker->call(
            [&]() {
                // Also add a delayed callback to make sure any additional data is eventually delivered
                ws->m_dcid = worker->delayed_call(1000, &WebSocket::delayed_send, ws.get());
            }, mxb::Worker::EXECUTE_AUTO);

        // All the connections need to be stored to be able to close them when the system is going down.
        std::lock_guard<std::mutex> guard(this_unit.lock);
        this_unit.connections.emplace_back(std::move(ws));
    }
}

WebSocket::WebSocket(int fd, MHD_UpgradeResponseHandle* urh, std::function<std::string ()> cb)
    : m_fd(fd)
    , m_urh(urh)
    , m_cb(cb)
{
    this->handler = &WebSocket::poll_handler;
    setnonblocking(m_fd);
}

WebSocket::~WebSocket()
{
    auto worker = mxs::MainWorker::get();

    if (auto id = m_dcid)
    {
        m_dcid = 0;
        worker->cancel_delayed_call(id);
    }

    worker->remove_fd(m_fd);

    // Send the Close command. If it fails then it fails but at least we tried.
    uint8_t buf[2] = {0x88};
    write(m_fd, buf, sizeof(buf));

    MHD_upgrade_action(m_urh, MHD_UPGRADE_ACTION_CLOSE);
}

// static
void WebSocket::close(WebSocket* ws)
{
    std::lock_guard<std::mutex> guard(this_unit.lock);

    auto it = std::find_if(
        this_unit.connections.begin(), this_unit.connections.end(), [ws](const auto& p) {
            return p.get() == ws;
        });

    mxb_assert(it != this_unit.connections.end());
    this_unit.connections.erase(it);
}

// static
void WebSocket::shutdown()
{
    std::lock_guard<std::mutex> guard(this_unit.lock);
    this_unit.connections.clear();
}

// static
uint32_t WebSocket::poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events)
{
    WebSocket* ws = static_cast<WebSocket*>(data);
    bool ok = false;

    // We only expect EPOLLOUT events delivered as a result of the socket being empty again. All other
    // events are treated as errors.
    if ((events & EPOLLOUT) && ws->send())
    {
        ok = true;
    }

    if (!ok)
    {
        // Something went wrong, close the connection
        WebSocket::close(ws);
    }

    return events;
}

bool WebSocket::send()
{
    WebSocket::Result res = MORE;

    do
    {
        if (m_buffer.empty())
        {
            std::string data = m_cb();

            if (data.empty())
            {
                break;
            }

            enqueue_frame(data);
        }

        res = drain();
    }
    while (res == MORE);

    return res != ERROR;
}

bool WebSocket::delayed_send(mxb::Worker::Call::action_t action)
{
    if (action == mxb::Worker::Call::CANCEL)
    {
        return false;
    }

    bool rval = send();

    if (!rval)
    {
        // We must not cancel the delayed call if we return false from this function. We also don't want
        // to delete the WebSocket here to make sure the poll handler always does it. If the write fails
        // here, an EPOLLERR event should be delivered.
        m_dcid = 0;
    }

    return rval;
}

void WebSocket::enqueue_frame(const std::string& data)
{
    // First bit is for text type frame, last bit is for final frame
    uint8_t header[10] = {0x81};

    if (data.size() < 126)
    {
        header[1] = data.size();
        m_buffer.insert(m_buffer.end(), header, header + 2);
    }
    else if (data.size() < 65535)
    {
        header[1] = 126;
        header[2] = data.size() >> 8;
        header[3] = data.size();
        m_buffer.insert(m_buffer.end(), header, header + 4);
    }
    else
    {
        header[1] = 127;
        header[2] = data.size() >> 56;
        header[3] = data.size() >> 48;
        header[4] = data.size() >> 40;
        header[5] = data.size() >> 32;
        header[6] = data.size() >> 24;
        header[7] = data.size() >> 16;
        header[8] = data.size() >> 8;
        header[9] = data.size();
        m_buffer.insert(m_buffer.end(), header, header + 10);
    }

    m_buffer.insert(m_buffer.end(), data.begin(), data.end());
}

WebSocket::Result WebSocket::drain()
{
    int64_t n = ::write(m_fd, m_buffer.data(), m_buffer.size());

    if (n == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return FULL;
        }
        else
        {
            return ERROR;
        }
    }

    m_buffer.erase(m_buffer.begin(), std::next(m_buffer.begin(), n));

    return m_buffer.empty() ? MORE : FULL;
}
