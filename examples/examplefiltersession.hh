/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>

class ExampleFilter;

class ExampleFilterSession : public maxscale::FilterSession
{
    // Prevent copy-constructor and assignment operator usage
    ExampleFilterSession(const ExampleFilterSession&);
    ExampleFilterSession& operator = (const ExampleFilterSession&);

public:
    ~ExampleFilterSession();

    // Called when a client session has been closed
    void close();

    // Create a new filter session
    static ExampleFilterSession* create(MXS_SESSION* pSession, const ExampleFilter* pFilter);

    // Handle a query from the client
    int routeQuery(GWBUF* pPacket);

    // Handle a reply from server
    int clientReply(GWBUF* pPacket);

private:
    // Used in the create function
    ExampleFilterSession(MXS_SESSION* pSession);
};
