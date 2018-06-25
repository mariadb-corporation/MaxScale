/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "nullfilter"
#include "nullfiltersession.hh"

NullFilterSession::NullFilterSession(MXS_SESSION* pSession, const NullFilter* pFilter)
    : maxscale::FilterSession(pSession)
    , m_filter(*pFilter)
{
}

NullFilterSession::~NullFilterSession()
{
}

//static
NullFilterSession* NullFilterSession::create(MXS_SESSION* pSession, const NullFilter* pFilter)
{
    return new NullFilterSession(pSession, pFilter);
}
