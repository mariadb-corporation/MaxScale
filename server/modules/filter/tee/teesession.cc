/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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
                       const mxb::Regex& match, const mxb::Regex& exclude, bool sync)
    : mxs::FilterSession(session, service)
    , m_client(client)
    , m_match(match)
    , m_exclude(exclude)
    , m_sync(sync)
{
    if (m_sync)
    {
        auto reply = [this](GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) {
                handle_reply(reply, true);
            };

        auto err = [this](GWBUF* err, mxs::Target* target, const mxs::Reply& reply) {
                MXS_INFO("Branch connection failed: %s", mxs::extract_error(err).c_str());
                // Note: we don't own the error passed to this function
                m_pSession->kill(gwbuf_clone_shallow(err));
            };

        m_client->set_notify(reply, err);
    }
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

    return new TeeSession(session, service, client, config.match, config.exclude, config.sync);
}

TeeSession::~TeeSession()
{
    delete m_client;
}

bool TeeSession::routeQuery(GWBUF* queue)
{
    if (m_client && m_sync && m_branch_replies + m_main_replies > 0)
    {
        MXS_INFO("Waiting for replies: %d from branch, %d from main", m_branch_replies, m_main_replies);
        m_queue.push_back(queue);
        return true;
    }

    if (m_client && query_matches(queue) && m_client->queue_query(gwbuf_deep_clone(queue)))
    {
        if (m_sync && mxs_mysql_command_will_respond(mxs_mysql_get_command(queue)))
        {
            // These two could be combined into one uint8_t as they never go above one but having them as
            // separate variables makes debugging easier.
            mxb_assert(m_branch_replies + m_main_replies == 0);
            ++m_branch_replies;
            ++m_main_replies;
        }
    }

    return mxs::FilterSession::routeQuery(queue);
}

void TeeSession::handle_reply(const mxs::Reply& reply, bool is_branch)
{
    uint8_t& expected_replies = is_branch ? m_branch_replies : m_main_replies;

    if (expected_replies > 0 && reply.is_complete())
    {
        mxb_assert(expected_replies == 1);
        --expected_replies;
        MXS_INFO("%s reply complete", is_branch ? "Brach" : "Main");
    }

    if (m_branch_replies + m_main_replies == 0 && !m_queue.empty())
    {
        MXS_INFO("Both replies received, routing queued query: %s",
                 m_queue.front().get_sql().c_str());
        session_delay_routing(m_pSession, this, m_queue.front().release(), 0);
        m_queue.pop_front();
    }
}

bool TeeSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    handle_reply(reply, false);
    return mxs::FilterSession::clientReply(pPacket, down, reply);
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
        const auto& sql = buffer->get_sql();

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
