/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "rewitefilter"
#include "rewritesession.hh"
#include "rewritefilter.hh"

#include <maxscale/modutil.hh>

RewriteFilterSession::RewriteFilterSession(MXS_SESSION* pSession,
                                           SERVICE* pService,
                                           const std::shared_ptr<Settings>& sSettings)
    : maxscale::FilterSession(pSession, pService)
    , m_sSettings(sSettings)
{
}

void RewriteFilterSession::log_replacement(const std::string& from, const std::string& to)
{
    MXB_SINFO("Replace:\n" << from << "\nwith:\n" << to);
}

RewriteFilterSession::~RewriteFilterSession()
{
}

// static
RewriteFilterSession* RewriteFilterSession::create(MXS_SESSION* pSession,
                                                   SERVICE* pService,
                                                   const std::shared_ptr<Settings>& sSettings)
{
    return new RewriteFilterSession(pSession, pService, sSettings);
}

bool RewriteFilterSession::routeQuery(GWBUF* pBuffer)
{
    auto& settings = *m_sSettings.get();
    const auto& sql = pBuffer->get_sql();

    for (const auto& r : settings.rewriters)
    {
        std::string new_sql;
        if (r.replace(sql, &new_sql))
        {
            if (settings.log_replacement)
            {
                log_replacement(sql, new_sql);
            }
            gwbuf_free(pBuffer);
            pBuffer = modutil_create_query(new_sql.c_str());
            break;
        }
    }

    return mxs::FilterSession::routeQuery(pBuffer);
}
