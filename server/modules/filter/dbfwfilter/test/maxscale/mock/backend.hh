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
#include <map>
#include "routersession.hh"

namespace maxscale
{

namespace mock
{

/**
 * The abstract class Backend represents a backend.
 */
class Backend
{
    Backend(const Backend&);
    Backend& operator = (const Backend&);

public:
    virtual ~Backend();

    /**
     * Called to handle a statement from a "client".
     *
     * @param pSession    The originating router session.
     * @param pStatement  A buffer containing a statement.
     */
    virtual void handle_statement(RouterSession* pSession, GWBUF* pStatement) = 0;

    /**
     * Called when the backend should respond to the client.
     *
     * @param pSession  The router session to respond to.
     *
     * @return True, if the backend has additional responses to the router session.
     */
    virtual bool respond(RouterSession* pSession) = 0;

    /**
     * Whether the backend has a response for some router.
     *
     * @param pSession  A router session.
     *
     * @return True if there are responses for the router session.
     */
    virtual bool idle(const RouterSession* pSession) const = 0;

protected:
    Backend();
};

/**
 * The abstract class BufferBackend is a helper class for concrete
 * backend classes.
 */
class BufferBackend : public Backend
{
    BufferBackend(const BufferBackend&);
    BufferBackend& operator = (const BufferBackend&);

public:
    ~BufferBackend();

    bool respond(RouterSession* pSession);

    bool idle(const RouterSession* pSession) const;

protected:
    BufferBackend();

    /**
     * Enqueues a response for a particular router session.
     *
     * @param pSession   The session to enqueue the response for.
     * @param pResponse  The response.
     */
    void enqueue_response(RouterSession* pSession, GWBUF* pResponse);

private:
    typedef std::deque<GWBUF*> Responses;
    typedef std::map<const RouterSession*, Responses> SessionResponses;

    SessionResponses m_session_responses;
};

/**
 * The OkBackend is a concrete backend class that response with an
 * OK packet to all statements.
 */
class OkBackend : public BufferBackend
{
    OkBackend(const OkBackend&);
    OkBackend& operator = (const OkBackend&);

public:
    OkBackend();

    void handle_statement(RouterSession* pSession, GWBUF* pStatement);
};

}

}
