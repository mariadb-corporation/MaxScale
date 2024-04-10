/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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

TeeSession::TeeSession(MXS_SESSION* session, SERVICE* service, std::unique_ptr<LocalClient> client,
                       const mxb::Regex& match, const mxb::Regex& exclude, bool sync)
    : mxs::FilterSession(session, service)
    , m_client(std::move(client))
    , m_match(match)
    , m_exclude(exclude)
    , m_sync(sync)
{
    if (m_sync)
    {
        auto reply_cb = [this](GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) {
                handle_reply(reply, true);
            };

        auto err_cb = [this](const std::string& err, mxs::Target* target, const mxs::Reply& reply) {
                MXB_INFO("Branch connection failed: %s", err.c_str());
                m_pSession->kill();
            };

        m_client->set_notify(reply_cb, err_cb);
    }
}

TeeSession* TeeSession::create(Tee* my_instance, MXS_SESSION* session, SERVICE* service)
{
    std::unique_ptr<LocalClient> client;
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
            MXB_ERROR("Failed to create local client connection to '%s'",
                      config.target->name());
            return nullptr;
        }
    }

    return new TeeSession(session, service, std::move(client), config.match, config.exclude, config.sync);
}

bool TeeSession::routeQuery(GWBUF&& queue)
{
    if (m_client && m_sync && m_branch_replies + m_main_replies > 0)
    {
        MXB_INFO("Waiting for replies: %d from branch, %d from main", m_branch_replies, m_main_replies);
        m_queue.push_back(std::move(queue));
        return true;
    }

    if (m_client && query_matches(queue) && m_client->queue_query(queue.shallow_clone()))
    {
        if (m_sync && protocol_data().will_respond(queue))
        {
            // These two could be combined into one uint8_t as they never go above one but having them as
            // separate variables makes debugging easier.
            mxb_assert(m_branch_replies + m_main_replies == 0);
            ++m_branch_replies;
            ++m_main_replies;
        }
    }

    return mxs::FilterSession::routeQuery(std::move(queue));
}

void TeeSession::handle_reply(const mxs::Reply& reply, bool is_branch)
{
    uint8_t& expected_replies = is_branch ? m_branch_replies : m_main_replies;

    if (expected_replies > 0 && reply.is_complete())
    {
        mxb_assert(expected_replies == 1);
        --expected_replies;
        MXB_INFO("%s reply complete", is_branch ? "Brach" : "Main");
    }

    if (m_branch_replies + m_main_replies == 0 && !m_queue.empty())
    {
        MXB_INFO("Both replies received, routing queued query: %s",
                 get_sql_string(m_queue.front()).c_str());
        m_pSession->delay_routing(this, std::move(m_queue.front()), 0ms);
        m_queue.pop_front();
    }
}

bool TeeSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    handle_reply(reply, false);
    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}

json_t* TeeSession::diagnostics() const
{
    return NULL;
}

bool TeeSession::query_matches(const GWBUF& buffer)
{
    bool rval = true;

    if (m_match || m_exclude)
    {
        const auto& sql = get_sql_string(buffer);

        if (!sql.empty())
        {
            if (m_match && !m_match.match(sql))
            {
                MXB_INFO("Query does not match the 'match' pattern: %s", sql.c_str());
                rval = false;
            }
            else if (m_exclude && m_exclude.match(sql))
            {
                MXB_INFO("Query matches the 'exclude' pattern: %s", sql.c_str());
                rval = false;
            }
        }
    }

    return rval;
}
