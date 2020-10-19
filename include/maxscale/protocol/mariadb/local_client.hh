/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <deque>

#include <maxbase/poll.h>
#include <maxscale/buffer.hh>
#include <maxscale/service.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

/** A DCB-like client abstraction which ignores responses */
class LocalClient : public mxs::Component
{
    LocalClient(const LocalClient&);
    LocalClient& operator=(const LocalClient&);

public:
    ~LocalClient();

    /**
     * Create a local client for a service
     *
     * @param session Client session
     * @param service Target to connect to
     *
     * @return New virtual client or NULL on error
     */
    static LocalClient* create(MXS_SESSION* session, mxs::Target* target);

    /**
     * Connect to the target
     *
     * @return True on success, false on error
     */
    bool connect();

    /**
     * Queue a new query for execution
     *
     * @param buffer Buffer containing the query. The function takes ownership of the buffer.
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

    //
    // API function implementations for mxs::Component
    //
    int32_t routeQuery(GWBUF* buffer) override;

    int32_t clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, GWBUF* error, mxs::Endpoint* down,
                     const mxs::Reply& reply) override;

private:
    LocalClient() = default;

    bool                           m_self_destruct {false};
    std::unique_ptr<mxs::Endpoint> m_down;
};
