/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "rewritefilter"
#include "rewritesession.hh"
#include <maxscale/protocol/mariadb/mysql.hh>
#include "rewritefilter.hh"

RewriteFilterSession::RewriteFilterSession(MXS_SESSION* pSession,
                                           SERVICE* pService,
                                           const std::shared_ptr<const SessionData>& sSettings)
    : maxscale::FilterSession(pSession, pService)
    , m_sSession_data(sSettings)
{
}

void RewriteFilterSession::log_replacement(const std::string& from,
                                           const std::string& to,
                                           bool what_if)
{
    std::ostringstream os;
    if (what_if)
    {
        os << "what_if is set. Would r";
    }
    else
    {
        os << 'R';
    }
    os << "eplace \"" << from << "\" with \"" << to << '"';
    MXB_NOTICE("%s", os.str().c_str());
}

RewriteFilterSession::~RewriteFilterSession()
{
}

// static
RewriteFilterSession* RewriteFilterSession::create(MXS_SESSION* pSession,
                                                   SERVICE* pService,
                                                   const std::shared_ptr<const SessionData>& sSettings)
{
    return new RewriteFilterSession(pSession, pService, sSettings);
}

bool RewriteFilterSession::routeQuery(GWBUF&& buffer)
{
    GWBUF* pBuffer = mxs::gwbuf_to_gwbufptr(std::move(buffer));
    auto& session_data = *m_sSession_data.get();
    const auto& sql = get_sql_string(*pBuffer);
    const auto* pSql_to_match = &sql;

    std::string new_sql;
    std::string sql_before;     // for logging
    bool continued = false;     // also for logging. Avoid a copy unless match-and-continue happened

    for (const auto& r : session_data.rewriters)
    {
        bool do_logging = session_data.settings.log_replacement || r->template_def().what_if;
        if (do_logging && continued)
        {
            sql_before = *pSql_to_match;
        }

        if (r->replace(*pSql_to_match, &new_sql))
        {
            if (do_logging)
            {
                const auto& from = continued ? sql_before : sql;
                log_replacement(from, new_sql, r->template_def().what_if);
            }

            if (!r->template_def().what_if)
            {
                gwbuf_free(pBuffer);
                pBuffer = mxs::gwbuf_to_gwbufptr(mariadb::create_query(new_sql));
            }

            if (r->template_def().continue_if_matched)
            {
                continued = true;
                pSql_to_match = &new_sql;
            }
            else
            {
                break;
            }
        }
    }

    return mxs::FilterSession::routeQuery(mxs::gwbufptr_to_gwbuf(pBuffer));
}
