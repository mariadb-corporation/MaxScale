/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/adminclient.hh"
#include "maxscale/httpparser.hh"

#include <string>
#include <sstream>

#include <maxscale/atomic.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/log_manager.h>
#include <maxscale/thread.h>
#include <maxscale/spinlock.hh>

using std::string;
using std::stringstream;
using mxs::SpinLockGuard;

AdminClient::AdminClient(int fd, const struct sockaddr_storage& addr, int timeout):
    m_fd(fd),
    m_last_activity(atomic_read_int64(&hkheartbeat)),
    m_addr(addr)
{
}

void AdminClient::close_connection()
{
    SpinLockGuard guard(m_lock);

    if (m_fd != -1)
    {
        close(m_fd);
        m_fd = -1;
    }
}

AdminClient::~AdminClient()
{
    close_connection();
}

static bool read_request(int fd, string& output)
{
    while (true)
    {
        char buf[1024];
        int bufsize = sizeof(buf) - 1;

        int rc = read(fd, buf, bufsize);

        if (rc == -1)
        {
            return false;
        }

        buf[rc] = '\0';
        output += buf;

        if (rc < bufsize)
        {
            /** Complete request read */
            break;
        }
    }

    return true;
}

static bool write_response(int fd, string input)
{
    return write(fd, input.c_str(), input.length()) != -1;
}

void AdminClient::process()
{
    string request;
    atomic_write_int64(&m_last_activity, hkheartbeat);

    if (read_request(m_fd, request))
    {
        SHttpParser parser(HttpParser::parse(request));

        string status = parser.get() ? "200 OK" : "400 Bad Request";

        stringstream resp;
        resp << "HTTP/1.1 " << status << "\r\n\r\n" << parser->get_body() << "\r\n";

        atomic_write_int64(&m_last_activity, hkheartbeat);
        write_response(m_fd, resp.str());
    }
    else
    {
        MXS_ERROR("Failed to read client request: %d, %s", errno, mxs_strerror(errno));
    }
}
