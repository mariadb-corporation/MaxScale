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

#define MXS_MODULE_NAME "hintrouter"
#include "hintroutersession.hh"
#include <algorithm>
#include <functional>


HintRouterSession::HintRouterSession(MXS_SESSION*    pSession,
                                     HintRouter*     pRouter,
                                     const Backends& backends)
    : maxscale::RouterSession(pSession)
    , m_pRouter(pRouter)
    , m_backends(backends)
    , m_surplus_replies(0)
{
    HR_ENTRY();
}


HintRouterSession::~HintRouterSession()
{
    HR_ENTRY();
}


void HintRouterSession::close()
{
    HR_ENTRY();
    m_backends.clear();
}

namespace
{

/**
 * Writer is a function object that writes a clone of a provided GWBUF,
 * to each dcb it is called with.
 */
class Writer : std::unary_function<Dcb, bool>
{
public:
    Writer(GWBUF* pPacket)
        : m_pPacket(pPacket)
    {
    }

    bool operator()(Dcb& dcb)
    {
        bool rv = false;

        GWBUF* pPacket = gwbuf_clone(m_pPacket);

        if (pPacket)
        {
            SERVER* pServer = dcb.server();
            HR_DEBUG("Writing packet to %p %s.", dcb.get(), pServer ? pServer->name : "(null)");

            rv = dcb.write(pPacket);
        }

        return rv;
    }

private:
    GWBUF* m_pPacket;
};


/**
 * HintMatcher is a function object that when invoked with a dcb, checks
 * whether the dcb matches the hint(s) that was given when the HintMatcher
 * was created.
 */
class HintMatcher : std::unary_function<const Dcb, bool>
{
public:
    HintMatcher(HINT* pHint)
        : m_pHint(pHint)
    {
    }

    bool operator()(const Dcb& dcb)
    {
        bool match = false;

        SERVER* pServer = dcb.server();

        if (pServer)
        {
            HINT* pHint = m_pHint;

            while (!match && pHint)
            {
                switch (pHint->type)
                {
                case HINT_ROUTE_TO_MASTER:
                    if (SERVER_IS_MASTER(pServer))
                    {
                        match = true;
                    }
                    break;

                case HINT_ROUTE_TO_SLAVE:
                    if (SERVER_IS_SLAVE(pServer))
                    {
                        match = true;
                    }
                    break;

                case HINT_ROUTE_TO_NAMED_SERVER:
                    {
                        const char* zName = static_cast<const char*>(pHint->data);

                        if (strcmp(zName, pServer->name) == 0)
                        {
                            match = true;
                        }
                    }
                    break;

                case HINT_ROUTE_TO_UPTODATE_SERVER:
                case HINT_ROUTE_TO_ALL:
                case HINT_PARAMETER:
                    MXS_ERROR("HINT not handled.");
                    ss_dassert(false);
                    break;
                }

                pHint = pHint->next;
            }
        }

        return match;
    }

private:
    HINT* m_pHint;
};

}

int32_t HintRouterSession::routeQuery(GWBUF* pPacket)
{
    HR_ENTRY();

    int32_t continue_routing = 1;

    if (pPacket->hint)
    {
        // At least one hint => look for match.
        HR_DEBUG("Hint, looking for match.");

        Backends::iterator i = std::find_if(m_backends.begin(),
                                            m_backends.end(),
                                            HintMatcher(pPacket->hint));

        if (i != m_backends.end())
        {
            Dcb& dcb = *i;
            HR_DEBUG("Writing packet to %s.", dcb.server()->name);

            dcb.write(pPacket);

            // Rotate dcbs so that we get round robin as far as the slaves are concerned.
            m_backends.push_front(m_backends.back()); // Push last to first
            m_backends.pop_back(); // Remove last element

            m_surplus_replies = 0;
        }
        else
        {
            MXS_ERROR("No backend to write to.");
            continue_routing = 0;
        }
    }
    else
    {
        // No hint => all.
        HR_DEBUG("No hints, writing to all.");

        size_t n_writes = std::count_if(m_backends.begin(), m_backends.end(), Writer(pPacket));
        gwbuf_free(pPacket);

        if (n_writes != 0)
        {
            m_surplus_replies = n_writes - 1;
        }
        else
        {
            MXS_ERROR("Nothing could be written, terminating session.");

            continue_routing = 0;
        }
    }

    return continue_routing;
}


void HintRouterSession::clientReply(GWBUF* pPacket, DCB* pBackend)
{
    HR_ENTRY();

    SERVER* pServer = pBackend->server;

    if (m_surplus_replies == 0)
    {
        HR_DEBUG("Returning packet from %s.", pServer ? pServer->name : "(null)");

        MXS_SESSION_ROUTE_REPLY(pBackend->session, pPacket);
    }
    else
    {
        HR_DEBUG("Ignoring reply packet from %s.", pServer ? pServer->name : "(null)");

        --m_surplus_replies;
        gwbuf_free(pPacket);
    }
}


void HintRouterSession::handleError(GWBUF*             pMessage,
                                    DCB*               pProblem,
                                    mxs_error_action_t action,
                                    bool*              pSuccess)
{
    HR_ENTRY();

    ss_dassert(pProblem->dcb_role == DCB_ROLE_BACKEND_HANDLER);

    MXS_SESSION* pSession = pProblem->session;
    mxs_session_state_t sesstate = pSession->state;

    switch (action)
    {
    case ERRACT_REPLY_CLIENT:
        {
            /* React to failed authentication, send message to client */
            if (sesstate == SESSION_STATE_ROUTER_READY)
            {
                /* Send error report to client */
                GWBUF* pCopy = gwbuf_clone(pMessage);
                if (pCopy)
                {
                    DCB* pClient = pSession->client_dcb;
                    pClient->func.write(pClient, pCopy);
                }
            }
            *pSuccess = false;
        }
        break;

    case ERRACT_NEW_CONNECTION:
        {
            HR_DEBUG("ERRACT_NEW_CONNECTION");
            *pSuccess = true;
        }
        break;

    default:
        ss_dassert(!true);
        *pSuccess = false;
    }
}
