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
#include <memory>
#include <maxscale/filter.h>
#include "../filtermodule.hh"

namespace maxscale
{

namespace mock
{

/**
 * An instance of Upstream represents an upstream object of a filter.
 */
class Upstream : public MXS_FILTER_SESSION
{
    Upstream(const Upstream&);
    Upstream& operator = (const Upstream&);

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
        virtual int32_t clientReply(GWBUF* pResponse) = 0;

        /**
         * Called when @reset is called on the @c Upstream instance.
         */
        virtual void reset();
    };

    /**
     * Constructor
     *
     * @param pHandler  Optional response handler.
     */
    Upstream(Handler* pHandler = NULL);
    ~Upstream();

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
     * Reset the Upstream object. The number of counted responsed will
     * be set to 0. If the Upstream object has a handler, then its @c reset
     * function will be called as well.
     */
    void reset();

    /**
     * Set this object as upstream filter of provided filter.
     *
     * @param session  The filter session whose upstream filter should be set.
     */
    void set_as_upstream_on(FilterModule::Session& session);

private:
    int32_t clientReply(GWBUF* pResponse);

    static int32_t clientReply(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pSession, GWBUF* pResponse);

private:
    MXS_FILTER m_instance;
    Handler*   m_pHandler;
    size_t     m_n_responses;
};

}

}
