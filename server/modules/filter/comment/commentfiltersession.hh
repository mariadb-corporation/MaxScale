#pragma once
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

#include <maxscale/cppdefs.hh>
#include <maxscale/filter.hh>
#include <string>

class CommentFilter;

class CommentFilterSession : public maxscale::FilterSession
{
    // Prevent copy-constructor and assignment operator usage
    CommentFilterSession(const CommentFilterSession&);
    CommentFilterSession& operator = (const CommentFilterSession&);

public:
    ~CommentFilterSession();

    // Called when a client session has been closed
    void close();

    // Create a new filter session
    static CommentFilterSession* create(MXS_SESSION* pSession, const CommentFilter* pFilter);

    // Handle a query from the client
    int routeQuery(GWBUF* pPacket);

    // Handle a reply from server
    int clientReply(GWBUF* pPacket);

private:

    // Used in the create function
    CommentFilterSession(MXS_SESSION* pSession, const CommentFilter* pFilter);
    const CommentFilter& m_filter;
    std::string parseComment(std::string comment);
};
