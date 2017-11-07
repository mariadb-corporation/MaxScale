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

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "examplefilter"

#include "examplefiltersession.hh"
#include "examplefilter.hh"

ExampleFilterSession::ExampleFilterSession(MXS_SESSION* pSession)
    : mxs::FilterSession(pSession)
{
}

ExampleFilterSession::~ExampleFilterSession()
{
}

//static
ExampleFilterSession* ExampleFilterSession::create(MXS_SESSION* pSession, const ExampleFilter* pFilter)
{
    return new ExampleFilterSession(pSession);
}

int ExampleFilterSession::routeQuery(GWBUF* pPacket)
{
    return mxs::FilterSession::routeQuery(pPacket);
}

int ExampleFilterSession::clientReply(GWBUF* pPacket)
{
    return mxs::FilterSession::clientReply(pPacket);
}
