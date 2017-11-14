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
#include <deque>
#include <maxscale/router.hh>
#include "../filtermodule.hh"
#include "mock.hh"

namespace maxscale
{

namespace mock
{

class Backend;

/**
 * An instance of RouterSession is a router to which a filter forwards
 * data.
 */
class RouterSession : private MXS_ROUTER_SESSION
{
    RouterSession(const RouterSession&);
    RouterSession& operator = (const RouterSession&);

public:
    /**
     * Constructor
     *
     * @param pBackend  The backend associated with the router.
     */
    RouterSession(Backend* pBackend);
    ~RouterSession();

    /**
     * Set the router as the downstream filter of a particular filter.
     * The filter will at the same time become the upstream filter of
     * this router.
     *
     * @param pFilter_session  The filter to set this router as downstream
     *                         filter of.
     */
    void set_as_downstream_on(FilterModule::Session* pFilter_session);

    /**
     * Called by the backend to deliver a response.
     *
     * @return Whatever the upstream filter returns.
     */
    int32_t clientReply(GWBUF* pResponse);

    /**
     * Causes the router to make its associated backend deliver a response
     * to this router, which will then deliver it forward to its associated
     * upstream filter.
     *
     * @return True if there are additional responses to deliver.
     */
    bool respond();

    /**
     * Are there responses available.
     *
     * @return True, if there are no responses, false otherwise.
     */
    bool idle() const;

private:
    int32_t routeQuery(MXS_ROUTER* pInstance, GWBUF* pStatement);

    static int32_t routeQuery(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pRouter_session, GWBUF* pStatement);

private:
    MXS_ROUTER             m_instance;
    Backend*               m_pBackend;
    FilterModule::Session* m_pUpstream_filter_session;
};

}

}
