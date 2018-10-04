/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb_client.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/utils.h>

#ifdef EPOLLRDHUP
#define ERROR_EVENTS (EPOLLRDHUP | EPOLLHUP | EPOLLERR)
#else
#define ERROR_EVENTS (EPOLLHUP | EPOLLERR)
#endif

static const uint32_t poll_events = EPOLLIN | EPOLLOUT | EPOLLET | ERROR_EVENTS;

LocalClient::LocalClient(MYSQL_session* session, MySQLProtocol* proto, int fd)
    : m_state(VC_WAITING_HANDSHAKE)
    , m_sock(fd)
    , m_expected_bytes(0)
    , m_client(*session)
    , m_protocol(*proto)
    , m_self_destruct(false)
{
    MXB_POLL_DATA::handler = LocalClient::poll_handler;
}

LocalClient::~LocalClient()
{
    if (m_state != VC_ERROR)
    {
        close();
    }
}

bool LocalClient::queue_query(GWBUF* buffer)
{
    GWBUF* my_buf = NULL;

    if (m_state != VC_ERROR && (my_buf = gwbuf_deep_clone(buffer)))
    {
        m_queue.push_back(my_buf);

        if (m_state == VC_OK)
        {
            drain_queue();
        }
    }

    return my_buf != NULL;
}

void LocalClient::self_destruct()
{
    GWBUF* buffer = mysql_create_com_quit(NULL, 0);
    queue_query(buffer);
    gwbuf_free(buffer);
    m_self_destruct = true;
}

void LocalClient::close()
{
    mxb::Worker* worker = mxb::Worker::get_current();
    mxb_assert(worker);
    worker->remove_fd(m_sock);
    ::close(m_sock);
}

void LocalClient::error()
{
    if (m_state != VC_ERROR)
    {
        close();
        m_state = VC_ERROR;
    }
}

void LocalClient::process(uint32_t events)
{

    if (events & EPOLLIN)
    {
        GWBUF* buf = read_complete_packet();

        if (buf)
        {
            if (m_state == VC_WAITING_HANDSHAKE)
            {
                if (gw_decode_mysql_server_handshake(&m_protocol, GWBUF_DATA(buf) + MYSQL_HEADER_LEN) == 0)
                {
                    GWBUF* response = gw_generate_auth_response(&m_client, &m_protocol, false, false, 0);
                    m_queue.push_front(response);
                    m_state = VC_RESPONSE_SENT;
                }
                else
                {
                    error();
                }
            }
            else if (m_state == VC_RESPONSE_SENT)
            {
                if (mxs_mysql_is_ok_packet(buf))
                {
                    m_state = VC_OK;
                }
                else
                {
                    error();
                }
            }

            gwbuf_free(buf);
        }
    }

    if (events & EPOLLOUT)
    {
        /** Queue is drained */
    }

    if (events & ERROR_EVENTS)
    {
        error();
    }

    if (m_queue.size() && m_state != VC_ERROR && m_state != VC_WAITING_HANDSHAKE)
    {
        drain_queue();
    }
    else if (m_state == VC_ERROR && m_self_destruct)
    {
        delete this;
    }
}

GWBUF* LocalClient::read_complete_packet()
{
    GWBUF* rval = NULL;

    while (true)
    {
        uint8_t buffer[1024];
        int rc = read(m_sock, buffer, sizeof(buffer));

        if (rc == -1)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                MXS_ERROR("Failed to read from backend: %d, %s", errno, mxs_strerror(errno));
                error();
            }
            break;
        }

        mxs::Buffer chunk(buffer, rc);
        m_partial.append(chunk);
        size_t len = m_partial.length();

        if (m_expected_bytes == 0 && len >= 3)
        {
            mxs::Buffer::iterator iter = m_partial.begin();
            m_expected_bytes = MYSQL_HEADER_LEN;
            m_expected_bytes += *iter++;
            m_expected_bytes += (*iter++ << 8);
            m_expected_bytes += (*iter++ << 16);
        }

        if (len >= m_expected_bytes)
        {
            /** Read complete packet. Reset expected byte count and make
             * the buffer contiguous. */
            m_expected_bytes = 0;
            m_partial.make_contiguous();
            rval = m_partial.release();
            break;
        }
    }

    return rval;
}

void LocalClient::drain_queue()
{
    bool more = true;

    while (m_queue.size() && more)
    {
        /** Grab a buffer from the queue */
        GWBUF* buf = m_queue.front().release();
        m_queue.pop_front();

        while (buf)
        {
            int rc = write(m_sock, GWBUF_DATA(buf), GWBUF_LENGTH(buf));

            if (rc > 0)
            {
                buf = gwbuf_consume(buf, rc);
            }
            else
            {
                if (rc == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    MXS_ERROR("Failed to write to backend: %d, %s", errno, mxs_strerror(errno));
                    error();
                }

                m_queue.push_front(buf);
                more = false;
                break;
            }
        }
    }
}

uint32_t LocalClient::poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events)
{
    LocalClient* client = static_cast<LocalClient*>(data);
    client->process(events);
    return 0;
}

LocalClient* LocalClient::create(MYSQL_session* session, MySQLProtocol* proto, const char* ip, uint64_t port)
{
    LocalClient* rval = NULL;
    sockaddr_storage addr;
    int fd = open_network_socket(MXS_SOCKET_NETWORK, &addr, ip, port);

    if (fd > 0 && (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0 || errno == EINPROGRESS))
    {
        LocalClient* relay = new(std::nothrow) LocalClient(session, proto, fd);

        if (relay)
        {
            mxb::Worker* worker = mxb::Worker::get_current();

            if (worker->add_fd(fd, poll_events, (MXB_POLL_DATA*)relay))
            {
                rval = relay;
            }
            else
            {
                relay->m_state = VC_ERROR;
                delete rval;
                rval = NULL;
            }
        }
    }

    if (rval == NULL && fd > 0)
    {
        ::close(fd);
    }
    return rval;
}

LocalClient* LocalClient::create(MYSQL_session* session, MySQLProtocol* proto, SERVICE* service)
{
    LocalClient* rval = NULL;
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER* listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener->port > 0)
        {
            /** Pick the first network listener */
            rval = create(session, proto, "127.0.0.1", service->ports->port);
            break;
        }
    }

    return rval;
}

LocalClient* LocalClient::create(MYSQL_session* session, MySQLProtocol* proto, SERVER* server)
{
    return create(session, proto, server->address, server->port);
}
