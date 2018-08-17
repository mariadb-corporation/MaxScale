#pragma once
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

#include <maxscale/ccdefs.hh>

#include <deque>

#include <maxbase/poll.h>
#include <maxscale/buffer.hh>
#include <maxscale/service.h>
#include <maxscale/protocol/mysql.h>

/** A DCB-like client abstraction which ignores responses */
class LocalClient: public MXB_POLL_DATA
{
    LocalClient(const LocalClient&);
    LocalClient& operator=(const LocalClient&);

public:
    ~LocalClient();

    /**
     * Create a local client for a service
     *
     * @param session Client session
     * @param service Service to connect to
     *
     * @return New virtual client or NULL on error
     */
    static LocalClient* create(MYSQL_session* session, MySQLProtocol* proto, SERVICE* service);
    static LocalClient* create(MYSQL_session* session, MySQLProtocol* proto, SERVER* server);

    /**
     * Queue a new query for execution
     *
     * @param buffer Buffer containing the query
     *
     * @return True if query was successfully queued
     */
    bool queue_query(GWBUF* buffer);

    /**
     * Destroy the client by sending a COM_QUIT to the backend
     *
     * @note After calling this function, object must be treated as a deleted object
     */
    void self_destruct();

private:
    static LocalClient* create(MYSQL_session* session, MySQLProtocol* proto, const char* ip, uint64_t port);
    LocalClient(MYSQL_session* session, MySQLProtocol* proto, int fd);
    static uint32_t poll_handler(MXB_POLL_DATA* data, void* worker, uint32_t events);
    void   process(uint32_t events);
    GWBUF* read_complete_packet();
    void   drain_queue();
    void   error();
    void   close();

    /** Client states */
    enum vc_state
    {
        VC_WAITING_HANDSHAKE, // Initial state
        VC_RESPONSE_SENT, // Handshake received and response sent
        VC_OK, // Authentication is complete, ready for queries
        VC_ERROR // Something went wrong
    };

    vc_state                m_state;
    int                     m_sock;
    mxs::Buffer             m_partial;
    size_t                  m_expected_bytes;
    std::deque<mxs::Buffer> m_queue;
    MYSQL_session           m_client;
    MySQLProtocol           m_protocol;
    bool                    m_self_destruct;
};
