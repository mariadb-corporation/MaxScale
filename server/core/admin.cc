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

#include "maxscale/admin.hh"

#include <climits>
#include <new>

#include <maxscale/atomic.h>
#include <maxscale/debug.h>
#include <maxscale/thread.h>
#include <maxscale/utils.h>

#define DEFAULT_ADMIN_HOST "127.0.0.1"
#define DEFAULT_ADMIN_PORT 8080

static AdminListener* admin = NULL;
static THREAD admin_thread;

// TODO: Read values from the configuration
static AdminConfig config = {DEFAULT_ADMIN_HOST, DEFAULT_ADMIN_PORT};

void admin_main(void* data)
{
    AdminListener* admin = reinterpret_cast<AdminListener*>(data);
    admin->start();
}

AdminConfig& mxs_admin_get_config()
{
    return config;
}

bool mxs_admin_init()
{
    ss_dassert(admin == NULL);
    bool rval = false;
    struct sockaddr_storage addr = {};
    int sock = open_network_socket(MXS_SOCKET_LISTENER, &addr, config.host.c_str(), config.port);

    if (sock > -1)
    {
        if (listen(sock, INT_MAX) == 0)
        {
            admin = new (std::nothrow) AdminListener(sock);

            if (admin)
            {
                if (thread_start(&admin_thread, admin_main, admin))
                {
                    rval = true;
                }
                else
                {
                    delete admin;
                    admin = NULL;
                }
            }
            else
            {
                MXS_OOM();
            }
        }
        else
        {
            MXS_ERROR("Failed to start listening on '[%s]:%u': %d, %s",
                      config.host.c_str(), config.port,  errno, mxs_strerror(errno));
            close(sock);
        }
    }

    return rval;
}

void mxs_admin_shutdown()
{
    if (admin)
    {
        admin->stop();
        thread_wait(admin_thread);
        delete admin;
        admin = NULL;
    }
}

AdminListener::AdminListener(int sock):
    m_socket(sock),
    m_active(0),
    m_timeout(10)
{
}

AdminListener::~AdminListener()
{
    close(m_socket);
}

void AdminListener::handle_clients()
{
    AdminClient* client = accept_client();

    if (client)
    {
        client->process();
        delete client;
    }
}

void AdminListener::start()
{
    atomic_write(&m_active, 1);

    while (atomic_read(&m_active))
    {
        MXS_EXCEPTION_GUARD(handle_clients());
    }
}

void AdminListener::stop()
{
    atomic_write(&m_active, 0);
}

AdminClient* AdminListener::accept_client()
{
    AdminClient* rval = NULL;
    struct sockaddr_storage addr = {};
    socklen_t len = sizeof (addr);
    int fd = accept(m_socket, (struct sockaddr*) &addr, &len);

    if (fd > -1)
    {
        setnonblocking(fd);
        rval = new AdminClient(fd, addr, m_timeout);
    }
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
        // TODO: Use epoll for this
        thread_millisleep(1);
    }
    else
    {
        MXS_ERROR("Failed to accept client: %d, %s\n", errno, mxs_strerror(errno));
    }

    return rval;
}
