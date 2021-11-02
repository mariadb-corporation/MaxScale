/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <deque>

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

    using NotifyCB = std::function<void (GWBUF*, const mxs::ReplyRoute&, const mxs::Reply&)>;
    using ErrorCB = std::function<void (GWBUF*, mxs::Target*, const mxs::Reply&)>;

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
     * Check if the client is still open
     *
     * The client can close on its own due to backend errors.
     *
     * @return True if the client is still open and queries can be queued to it
     */
    bool is_open() const
    {
        return m_down->is_open();
    }

    /**
     * Set reply notification callback
     *
     * These functions are the equivalent of clientReply and handleError calls and are called
     * with the same arguments with the exception that the error type is not passed to
     * the error handler.
     *
     * @param cb  Reply handler
     * @param err Error handler
     */
    void set_notify(NotifyCB cb, ErrorCB err)
    {
        mxb_assert_message(cb && err, "Both functions must be present and valid");
        m_cb = std::move(cb);
        m_err = std::move(err);
    }

    /**
     * Queue a new query for execution
     *
     * @param buffer Buffer containing the query. The function takes ownership of the buffer.
     *
     * @return True if query was successfully queued
     */
    bool queue_query(GWBUF* buffer);

    //
    // API function implementations for mxs::Component
    //
    bool routeQuery(GWBUF* buffer) override;

    bool clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, GWBUF* error,
                     mxs::Endpoint* down, const mxs::Reply& reply) override;

private:
    LocalClient() = default;

    std::unique_ptr<mxs::Endpoint> m_down;
    NotifyCB                       m_cb;
    ErrorCB                        m_err;
};
