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

#include <maxscale/alloc.h>
#include "hintrouter.hh"

HintRouterSession::HintRouterSession(MXS_SESSION*    pSession,
                                     HintRouter*     pRouter,
                                     const BackendMap& backends)
    : maxscale::RouterSession(pSession)
    , m_router(pRouter)
    , m_backends(backends)
    , m_master(NULL)
    , m_n_routed_to_slave(0)
    , m_surplus_replies(0)
{
    HR_ENTRY();
    update_connections();
}


HintRouterSession::~HintRouterSession()
{
    HR_ENTRY();
}


void HintRouterSession::close()
{
    HR_ENTRY();
    m_master = Dcb(NULL);
    m_slaves.clear();
    m_backends.clear();
}

namespace
{

/**
 * Writer is a function object that writes a clone of a provided GWBUF,
 * to each dcb it is called with.
 */
class Writer : std::unary_function<HintRouterSession::MapElement, bool>
{
public:
    Writer(GWBUF* pPacket)
        : m_pPacket(pPacket)
    {}

    bool operator()(HintRouterSession::MapElement& elem)
    {
        bool rv = false;
        Dcb& dcb = elem.second;
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
    {}

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

bool HintRouterSession::route_to_slave(GWBUF* pPacket)
{
    bool success = false;
    // Find a valid slave
    size_type size = m_slaves.size();
    size_type begin = m_n_routed_to_slave % size;
    size_type limit = begin + size;
    for (size_type curr = begin; curr != limit; curr++)
    {
        Dcb& candidate = m_slaves.at(curr % size);
        if (SERVER_IS_SLAVE(candidate.server()))
        {
            success = candidate.write(pPacket);
            if (success)
            {
                break;
            }
        }
    }
    /* It is (in theory) possible, that none of the slaves in the slave-array are
     * working (or have been promoted to master) and the previous master is now
     * a slave. In this situation, re-arranging the dcb:s will help. */
    if (!success)
    {
        update_connections();
        for (size_type curr = begin; curr != limit; curr++)
        {
            Dcb& candidate = m_slaves.at(curr % size);
            success = candidate.write(pPacket);
            if (success)
            {
                break;
            }
        }
    }

    if (success)
    {
        m_n_routed_to_slave++;
    }
    return success;
}

bool HintRouterSession::route_by_hint(GWBUF* pPacket, HINT* hint, bool ignore_errors)
{
    bool success = false;
    switch (hint->type)
    {
    case HINT_ROUTE_TO_MASTER:
        if (m_master.get())
        {
            // The master server should be already known, but may have changed
            if (SERVER_IS_MASTER(m_master.server()))
            {
                success = m_master.write(pPacket);
            }
            else
            {
                update_connections();
                if (m_master.get())
                {
                    HR_DEBUG("Writing packet to %s.", m_master.server()->name);
                    success = m_master.write(pPacket);
                }
            }
        }
        else if (!ignore_errors)
        {
            MXS_ERROR("Hint suggests routing to master when no master connected.");
        }
        break;

    case HINT_ROUTE_TO_SLAVE:
        if (m_slaves.size())
        {
            success = route_to_slave(pPacket);
        }
        else if (!ignore_errors)
        {
            MXS_ERROR("Hint suggests routing to slave when no slave connected.");
        }
        break;

    case HINT_ROUTE_TO_NAMED_SERVER:
        {
            string backend_name((hint->data) ? (const char*)(hint->data) : "");
            BackendMap::const_iterator iter = m_backends.find(backend_name);
            if (iter != m_backends.end())
            {
                HR_DEBUG("Writing packet to %s.", iter->second.server()->unique_name);
                success = iter->second.write(pPacket);
            }
            else if (!ignore_errors)
            {
                MXS_ERROR("Hint suggests routing to backend '%s' when no such backend connected.",
                          backend_name.c_str());
            }
        }
        break;

    case HINT_ROUTE_TO_ALL:
        {
            HR_DEBUG("Writing packet to %lu backends.", m_backends.size());
            size_type n_writes =
                std::count_if(m_backends.begin(), m_backends.end(), Writer(pPacket));
            if (n_writes != 0)
            {
                m_surplus_replies = n_writes - 1;
            }
            else if (!ignore_errors)
            {
                MXS_ERROR("Nothing could be written, terminating session.");
            }

            success = (n_writes == m_backends.size()); // Is this too strict?
            if (success)
            {
                gwbuf_free(pPacket);
            }
        }
        break;

    default:
        MXS_ERROR("Unsupported hint type '%d'", hint->type);
        break;
    }

    return success;
}

int32_t HintRouterSession::routeQuery(GWBUF* pPacket)
{
    HR_ENTRY();

    bool success = false;

    if (pPacket->hint)
    {
        /* At least one hint => look for match. Only use the later hints if the
         * first is unsuccessful. */
        HINT* current_hint = pPacket->hint;
        HR_DEBUG("Hint, looking for match.");
        while (!success && current_hint)
        {
            success = route_by_hint(pPacket, current_hint, true);
            current_hint = current_hint->next;
        }
    }

    if (!success)
    {
        // No hint => default action.
        HR_DEBUG("No hints or hint-based routing failed, falling back to default action.");
        HINT fake_hint = {};
        fake_hint.type = m_router->get_default_action();
        if (fake_hint.type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            fake_hint.data = MXS_STRDUP(m_router->get_default_server().c_str());
            // Ignore allocation error, it will just result in an error later on
        }
        success = route_by_hint(pPacket, &fake_hint, false);
        if (fake_hint.type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            MXS_FREE(fake_hint.data);
        }
    }

    if (!success)
    {
        gwbuf_free(pPacket);
    }
    return success;
}


void HintRouterSession::clientReply(GWBUF* pPacket, DCB* pBackend)
{
    HR_ENTRY();

    SERVER* pServer = pBackend->server;

    if (m_surplus_replies == 0)
    {
        HR_DEBUG("Returning packet from %s.", pServer ? pServer->unique_name : "(null)");

        MXS_SESSION_ROUTE_REPLY(pBackend->session, pPacket);
    }
    else
    {
        HR_DEBUG("Ignoring reply packet from %s.", pServer ? pServer->unique_name : "(null)");

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

void HintRouterSession::update_connections()
{
    /* Attempt to rearrange the dcb:s in the session such that the master and
     * slave containers are correct again. Do not try to make new connections,
     * since those would not have the correct session state anyways. */
    m_master = Dcb(NULL);
    m_slaves.clear();

    for (BackendMap::const_iterator iter = m_backends.begin();
         iter != m_backends.end(); iter++)
    {
        SERVER* server = iter->second.get()->server;
        if (SERVER_IS_MASTER(server))
        {
            if (!m_master.get())
            {
                m_master = iter->second;
            }
            else
            {
                MXS_WARNING("Found multiple master servers when updating connections.");
            }
        }
        else if (SERVER_IS_SLAVE(server))
        {
            m_slaves.push_back(iter->second);
        }
    }
}
