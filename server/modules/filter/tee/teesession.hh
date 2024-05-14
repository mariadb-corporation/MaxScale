/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxscale/filter.hh>
#include <maxscale/protocol/mariadb/local_client.hh>
#include <maxscale/pcre2.hh>

class Tee;

/**
 * A Tee session
 */
class TeeSession : public mxs::FilterSession
{
    TeeSession(const TeeSession&);
    const TeeSession& operator=(const TeeSession&);

public:
    static TeeSession* create(Tee* my_instance, MXS_SESSION* session, SERVICE* service);

    bool    routeQuery(GWBUF&& packet) override;
    bool    clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;
    json_t* diagnostics() const;

private:
    TeeSession(MXS_SESSION* session,
               SERVICE* service,
               std::unique_ptr<LocalClient> client,
               const mxb::Regex& match,
               const mxb::Regex& exclude,
               bool sync);
    bool query_matches(const GWBUF& buffer);
    void handle_reply(const mxs::Reply& reply, bool is_branch);

    std::unique_ptr<LocalClient> m_client;      /**< The client connection to the local service */
    // TODO: This looks wrong, the reference is lost if config is updated
    const mxb::Regex& m_match;
    const mxb::Regex& m_exclude;
    bool              m_sync;
    uint8_t           m_main_replies {0};
    uint8_t           m_branch_replies {0};
    std::deque<GWBUF> m_queue;
};
