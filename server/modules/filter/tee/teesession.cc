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

#include "teesession.hh"
#include "tee.hh"

#include <set>
#include <string>

/**
 * Detect loops in the filter chain.
 */
bool recursive_tee_usage(std::set<std::string>& services, SERVICE* service)
{
    if (!services.insert(service->name).second)
    {
        /** The service name was already in the set */
        return true;
    }

    for (int i = 0; i < service->n_filters; i++)
    {
        const char* module = filter_def_get_module_name(service->filters[i]);

        if (strcmp(module, "tee") == 0)
        {
            /*
             * Found a Tee filter, recurse down its path
             * if the service name isn't already in the hashtable.
             */
            Tee* inst = (Tee*)filter_def_get_instance(service->filters[i]);

            if (inst == NULL)
            {
                /**
                 * This tee instance hasn't been initialized yet and full
                 * resolution of recursion cannot be done now.
                 */
            }
            else if (recursive_tee_usage(services, inst->get_service()))
            {
                return true;
            }
        }
    }

    return false;
}

TeeSession::TeeSession(MXS_SESSION* session, LocalClient* client):
    mxs::FilterSession(session),
    m_client(client)
{
}

TeeSession* TeeSession::create(Tee* my_instance, MXS_SESSION* session)
{
    std::set<std::string> services;

    if (recursive_tee_usage(services, my_instance->get_service()))
    {
        MXS_ERROR("%s: Recursive use of tee filter in service.",
                  session->service->name);
        return NULL;
    }

    LocalClient* client = NULL;

    if (my_instance->user_matches(session_get_user(session)) &&
        my_instance->remote_matches(session_get_remote(session)))
    {
        if ((client = LocalClient::create(session, my_instance->get_service())) == NULL)
        {
            return NULL;
        }
    }

    return new (std::nothrow) TeeSession(session, client);
}

TeeSession::~TeeSession()
{
    delete m_client;
}

void TeeSession::close()
{
}

int TeeSession::routeQuery(GWBUF* queue)
{
    if (m_client)
    {
        m_client->queue_query(queue);
    }

    return mxs::FilterSession::routeQuery(queue);
}

void TeeSession::diagnostics(DCB *pDcb)
{
}

json_t* TeeSession::diagnostics_json() const
{
    return NULL;
}
