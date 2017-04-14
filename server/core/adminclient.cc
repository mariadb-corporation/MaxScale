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

#include <string>

#include <maxscale/atomic.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/log_manager.h>

using std::string;

AdminClient::AdminClient(int fd, const struct sockaddr_storage& addr, int timeout):
    m_fd(fd),
    m_timeout(timeout),
    m_addr(addr)
{

}

AdminClient::~AdminClient()
{
    close(m_fd);
}

static bool read_request_header(int fd, int timeout, string& output)
{
    int64_t start = atomic_read_int64(&hkheartbeat);

    while ((atomic_read_int64(&hkheartbeat) - start) / 10 < timeout)
    {
        char buf[1024];
        int rc = read(fd, buf, sizeof(buf));

        if (rc == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            return false;
        }
        else if (rc > 0)
        {
            buf[rc] = '\0';
            output += buf;

            if (output.find("\r\n\r\n") != std::string::npos)
            {
                break;
            }
        }
    }

    return true;
}

static bool write_response(int fd, int timeout, string input)
{
    int64_t start = atomic_read_int64(&hkheartbeat);

    while ((atomic_read_int64(&hkheartbeat) - start) / 10 < timeout && input.length() > 0)
    {
        int rc = write(fd, input.c_str(), input.length());

        if (rc == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            return false;
        }
        else if (rc > 0)
        {
            input.erase(0, rc);
        }
    }

    return true;

}

void AdminClient::process()
{
    string request;

    if (read_request_header(m_fd, m_timeout, request))
    {
        /** Send the Status-Line part of the response */
        string response = "HTTP/1.1 200 OK\r\n";

        response += "\r\n";

        write_response(m_fd, m_timeout, response);
    }
    else
    {
        MXS_ERROR("Failed to read client request: %d, %s", errno, mxs_strerror(errno));
    }
}
