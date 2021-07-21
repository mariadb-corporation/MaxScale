/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "teesession.hh"
#include "tee.hh"

#include <set>
#include <string>

#include <maxscale/listener.hh>
#include <maxscale/modutil.hh>

TeeSession::TeeSession(MXS_SESSION* session, SERVICE* service, LocalClient* client,
                       const mxb::Regex& match, const mxb::Regex& exclude)
    : mxs::FilterSession(session, service)
    , m_client(client)
    , m_match(match)
    , m_exclude(exclude)
{
}

TeeSession* TeeSession::create(Tee* my_instance, MXS_SESSION* session, SERVICE* service)
{
    LocalClient* client = nullptr;
    const auto& config = my_instance->config();
    bool user_matches = config.user.empty() || session->user() == config.user;
    bool remote_matches = config.source.empty() || session->client_remote() == config.source;

    if (my_instance->is_enabled() && user_matches && remote_matches)
    {
        if ((client = LocalClient::create(session, config.target)))
        {
            client->connect();
        }
        else
        {
            MXS_ERROR("Failed to create local client connection to '%s'",
                      config.target->name());
            return nullptr;
        }
    }

    return new TeeSession(session, service, client, config.match, config.exclude);
}

TeeSession::~TeeSession()
{
    delete m_client;
}

bool TeeSession::routeQuery(GWBUF* queue)
{
    if (m_client && query_matches(queue))
    {
        m_client->queue_query(gwbuf_deep_clone(queue));
    }

    return mxs::FilterSession::routeQuery(queue);
}

json_t* TeeSession::diagnostics() const
{
    return NULL;
}

bool TeeSession::query_matches(GWBUF* buffer)
{
    bool rval = true;

    if (m_match || m_exclude)
    {
        std::string sql = mxs::extract_sql(buffer);

        if (!sql.empty())
        {
            if (m_match && !m_match.match(sql))
            {
                MXS_INFO("Query does not match the 'match' pattern: %s", sql.c_str());
                rval = false;
            }
            else if (m_exclude && m_exclude.match(sql))
            {
                MXS_INFO("Query matches the 'exclude' pattern: %s", sql.c_str());
                rval = false;
            }
        }
    }

    return rval;
}
