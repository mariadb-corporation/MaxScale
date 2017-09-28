#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <deque>

#include <maxscale/buffer.hh>
#include <maxscale/service.h>
#include <maxscale/protocol/mysql.h>

/** A DCB-like client abstraction which ignores responses */
class LocalClient: public MXS_POLL_DATA
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
    static LocalClient* create(MXS_SESSION* session, SERVICE* service);

    /**
     * Queue a new query for execution
     *
     * @param buffer Buffer containing the query
     *
     * @return True if query was successfully queued
     */
    bool queue_query(GWBUF* buffer);

private:
    LocalClient(MXS_SESSION* session, int fd);
    static uint32_t poll_handler(struct mxs_poll_data* data, int wid, uint32_t events);
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
    MXS_SESSION*            m_session;
    MySQLProtocol           m_protocol;
};
