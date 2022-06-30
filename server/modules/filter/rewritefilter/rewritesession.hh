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

class Settings;

class RewriteFilterSession : public maxscale::FilterSession
{
public:
    ~RewriteFilterSession();

    static RewriteFilterSession* create(MXS_SESSION* pSession,
                                        SERVICE* pService,
                                        const std::shared_ptr<Settings>& sSettings
                                        );

    bool routeQuery(GWBUF* pBuffer) override final;

private:
    RewriteFilterSession(MXS_SESSION* pSession,
                         SERVICE* pService,
                         const std::shared_ptr<Settings>& sSettings);

    RewriteFilterSession(const RewriteFilterSession&);
    RewriteFilterSession& operator=(const RewriteFilterSession&);

private:
    void log_replacement(const std::string& from, const std::string& to);
    std::shared_ptr<Settings> m_sSettings;
};
