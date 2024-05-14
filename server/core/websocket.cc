/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <memory>
#include <mutex>
#include <vector>
#include <fcntl.h>
#include <sys/epoll.h>

#include <maxscale/utils.hh>
#include <maxscale/mainworker.hh>

#include "internal/websocket.hh"

namespace
{
static constexpr uint32_t EVENTS = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;

struct ThisUnit
{
    std::mutex                              lock;
    std::vector<std::unique_ptr<WebSocket>> connections;
} this_unit;

int setnonblocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl == -1)
    {
        MXB_ERROR("Can't GET fcntl for %i, errno = %d, %s.", fd, errno, mxb_strerror(errno));
        return 1;
    }

    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1)
    {
        MXB_ERROR("Can't SET fcntl for %i, errno = %d, %s", fd, errno, mxb_strerror(errno));
        return 1;
    }
    return 0;
}
}

// static
void WebSocket::create(int fd, MHD_UpgradeResponseHandle* urh, std::function<std::string ()> cb)
{
    auto worker = mxs::MainWorker::get();
    std::unique_ptr<WebSocket> ws(new WebSocket(fd, urh, cb));

    // Send the initial payload and then add it to the worker to see when the socket drains
    if (ws->send() && worker->add_pollable(EVENTS, ws.get()))
    {
        worker->call(
            [&]() {
                // Also add a delayed callback to make sure any additional data is eventually delivered
                ws->m_dcid = ws->dcall(1000ms, &WebSocket::delayed_send, ws.get());
            });

        // All the connections need to be stored to be able to close them when the system is going down.
        std::lock_guard<std::mutex> guard(this_unit.lock);
        this_unit.connections.emplace_back(std::move(ws));
    }
}

WebSocket::WebSocket(int fd, MHD_UpgradeResponseHandle* urh, std::function<std::string ()> cb)
    : mxb::Worker::Callable(mxs::MainWorker::get())
    , m_fd(fd)
    , m_urh(urh)
    , m_cb(cb)
{
    setnonblocking(m_fd);
}

WebSocket::~WebSocket()
{
    auto worker = mxs::MainWorker::get();

    if (auto id = m_dcid)
    {
        m_dcid = 0;
        cancel_dcall(id);
    }

    worker->remove_pollable(this);

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

int WebSocket::poll_fd() const
{
    return m_fd;
}

uint32_t WebSocket::handle_poll_events(mxb::Worker* worker, uint32_t events, Pollable::Context)
{
    bool ok = false;

    // We only expect EPOLLOUT events delivered as a result of the socket being empty again. All other
    // events are treated as errors.
    if ((events & EPOLLOUT) && send())
    {
        ok = true;
    }

    if (!ok)
    {
        // Something went wrong, close the connection
        WebSocket::close(this);
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

bool WebSocket::delayed_send()
{
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
