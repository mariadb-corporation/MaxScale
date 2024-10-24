/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <memory>
#include <string>
#include <maxscale/filter.hh>
#include "../filtermodule.hh"
#include "dcb.hh"

namespace maxscale
{

namespace mock
{

/**
 * An instance of Client represents a client. It can be used as the
 * upstream filter of another filter.
 */
class Client : public mxs::Routable
             , public Dcb::Handler
{
    Client(const Client&);
    Client& operator=(const Client&);

public:
    /**
     * A Handler can be used for processing responses.
     */
    class Handler
    {
    public:
        virtual ~Handler();

        /**
         * Called when a response is received from the backend.
         *
         * @param pResponse The response packet.
         *
         * @return 1 if processing should continue, 0 otherwise.
         */
        virtual int32_t backend_reply(GWBUF&& response, const mxs::ReplyRoute& down,
                                      const mxs::Reply& reply) = 0;

        /**
         * Called when a response is sent directly by a filter.
         *
         * @param pResponse The response packet.
         *
         * @return 1 if processing should continue, 0 otherwise.
         */
        virtual int32_t maxscale_reply(GWBUF&& response) = 0;

        /**
         * Called when @reset is called on the @c Client instance.
         */
        virtual void reset();
    };

    /**
     * Constructor
     *
     * @param zUser     The client of the session,
     * @param zHost     The host of the client.
     * @param pHandler  Optional response handler.
     */
    Client(const char* zUser,
           const char* zHost,
           Handler* pHandler = NULL);
    ~Client();

    /**
     * @return The name of the client.
     */
    const char* user() const;

    /**
     * @return The name of the host.
     */
    const char* host() const;

    /**
     * Set a response handler
     *
     * @param pHandler  The new response handler.
     *
     * @return The previous response handler.
     */
    Handler* set_handler(Handler* pHandler);

    /**
     * How many responses have been handled.
     *
     * @return The number of responses since last call to @c reset.
     */
    size_t n_responses() const;

    /**
     * Reset the Client object. The number of counted responded will
     * be set to 0. If the Client object has a handler, then its @c reset
     * function will be called as well.
     */
    void reset();

    /**
     * Get the mxs::Routable for this object
     */
    mxs::Routable* as_upstream()
    {
        return this;
    }

    bool routeQuery(GWBUF&& packet) override
    {
        return 0;
    }

    bool clientReply(GWBUF&& response, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    // Dcb::Handler
    int32_t write(GWBUF&& buffer) override;

private:
    std::string    m_user;
    std::string    m_host;
    Handler*       m_pHandler;
    size_t         m_n_responses;
    mxs::Routable* m_upstream;
};
}
}
