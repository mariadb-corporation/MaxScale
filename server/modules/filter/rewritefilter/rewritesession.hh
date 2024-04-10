/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>

struct SessionData;

class RewriteFilterSession : public maxscale::FilterSession
{
public:
    ~RewriteFilterSession();

    static RewriteFilterSession* create(MXS_SESSION* pSession,
                                        SERVICE* pService,
                                        const std::shared_ptr<const SessionData>& sSettings
                                        );

    bool routeQuery(GWBUF&& pBuffer) override final;

private:
    RewriteFilterSession(MXS_SESSION* pSession,
                         SERVICE* pService,
                         const std::shared_ptr<const SessionData>& sSettings);

    RewriteFilterSession(const RewriteFilterSession&);
    RewriteFilterSession& operator=(const RewriteFilterSession&);

private:
    void log_replacement(const std::string& from, const std::string& to, bool what_if);
    std::shared_ptr<const SessionData> m_sSession_data;
};
