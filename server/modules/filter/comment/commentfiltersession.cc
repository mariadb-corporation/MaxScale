/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "commentfilter"

#include "commentfiltersession.hh"
#include "commentfilter.hh"
#include <maxscale/modutil.hh>
#include <string>
#include <regex>

using namespace std;

CommentFilterSession::CommentFilterSession(MXS_SESSION* pSession, const CommentFilter* pFilter)
    : maxscale::FilterSession(pSession),
      m_filter(*pFilter)
{
}

CommentFilterSession::~CommentFilterSession()
{
}

//static
CommentFilterSession* CommentFilterSession::create(MXS_SESSION* pSession, const CommentFilter* pFilter)
{
    return new CommentFilterSession(pSession, pFilter);
}

void CommentFilterSession::close()
{
}

int CommentFilterSession::routeQuery(GWBUF* pPacket)
{
    if (modutil_is_SQL(pPacket))
    {
        string sql = mxs::extract_sql(pPacket);
        string comment = parseComment(m_filter.comment());
        string newsql = string("/* ").append(comment).append(" */").append(sql);
        pPacket = modutil_replace_SQL(pPacket, (char*)newsql.c_str());
        //maxscale expects contiguous memory to arrive from client so we must make the buffer contiguous
        //after using modutil_replace_SQL.
        GWBUF* pModified_packet = gwbuf_make_contiguous(pPacket);
        if (pModified_packet)
        {
            pPacket = pModified_packet;
        }
        else
        {
            gwbuf_free(pPacket);
            pPacket = NULL;
        }
    }

    return pPacket ? mxs::FilterSession::routeQuery(pPacket) : 1;
}

int CommentFilterSession::clientReply(GWBUF* pPacket)
{
    return mxs::FilterSession::clientReply(pPacket);
}
//TODO this probably should be refactored in some way in case we add more variables
string CommentFilterSession::parseComment(string comment)
{
    string ip = m_pSession->client_dcb->remote;
    string parsedComment = std::regex_replace(comment, std::regex("\\$IP"), ip);
    return parsedComment;
}
