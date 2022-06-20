/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>

class RewriteFilter;

class RewriteFilterSession : public maxscale::FilterSession
{
public:
    ~RewriteFilterSession();

    static RewriteFilterSession* create(MXS_SESSION* pSession,
                                        SERVICE* pService, const RewriteFilter* pFilter);

private:
    RewriteFilterSession(MXS_SESSION* pSession, SERVICE* pService, const RewriteFilter* pFilter);

    RewriteFilterSession(const RewriteFilterSession&);
    RewriteFilterSession& operator=(const RewriteFilterSession&);

private:
    const RewriteFilter& m_filter;
};
