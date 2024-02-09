/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// All log messages from this module are prefixed with this
#define MXB_MODULE_NAME "commentfilter"

#include "commentfiltersession.hh"
#include "commentfilter.hh"
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <string>
#include <regex>

using namespace std;

CommentFilterSession::CommentFilterSession(MXS_SESSION* pSession,
                                           SERVICE* pService,
                                           const CommentFilter* pFilter)
    : maxscale::FilterSession(pSession, pService)
    , m_inject(pFilter->config().inject.get())
{
}

CommentFilterSession::~CommentFilterSession()
{
}

// static
CommentFilterSession* CommentFilterSession::create(MXS_SESSION* pSession,
                                                   SERVICE* pService,
                                                   const CommentFilter* pFilter)
{
    return new CommentFilterSession(pSession, pService, pFilter);
}

bool CommentFilterSession::routeQuery(GWBUF&& packet)
{
    if (mariadb::is_com_query(packet))
    {
        string comment = parseComment(m_inject);
        packet = mariadb::create_query(mxb::cat("/* ", comment, " */", get_sql_string(packet)));
    }

    return mxs::FilterSession::routeQuery(std::move(packet));
}

bool CommentFilterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}
// TODO this probably should be refactored in some way in case we add more variables
string CommentFilterSession::parseComment(string comment)
{
    string ip = m_pSession->client_remote();
    string parsedComment = std::regex_replace(comment, std::regex("\\$IP"), ip);
    return parsedComment;
}
