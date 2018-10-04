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

#include "teesession.hh"
#include "tee.hh"

#include <set>
#include <string>

#include <maxscale/modutil.h>

TeeSession::TeeSession(MXS_SESSION* session,
                       LocalClient* client,
                       pcre2_code*  match,
                       pcre2_match_data* md_match,
                       pcre2_code* exclude,
                       pcre2_match_data* md_exclude)
    : mxs::FilterSession(session)
    , m_client(client)
    , m_match(match)
    , m_md_match(md_match)
    , m_exclude(exclude)
    , m_md_exclude(md_exclude)
{
}

TeeSession* TeeSession::create(Tee* my_instance, MXS_SESSION* session)
{
    LocalClient* client = NULL;
    pcre2_code* match = NULL;
    pcre2_code* exclude = NULL;
    pcre2_match_data* md_match = NULL;
    pcre2_match_data* md_exclude = NULL;

    if (my_instance->is_enabled()
        && my_instance->user_matches(session_get_user(session))
        && my_instance->remote_matches(session_get_remote(session)))
    {
        match = my_instance->get_match();
        exclude = my_instance->get_exclude();

        if ((match && (md_match = pcre2_match_data_create_from_pattern(match, NULL)) == NULL)
            || (exclude && (md_exclude = pcre2_match_data_create_from_pattern(exclude, NULL)) == NULL))
        {
            MXS_OOM();
            return NULL;
        }

        if ((client = LocalClient::create((MYSQL_session*)session->client_dcb->data,
                                          (MySQLProtocol*)session->client_dcb->protocol,
                                          my_instance->get_service())) == NULL)
        {
            MXS_ERROR("Failed to create local client connection to '%s'%s",
                      my_instance->get_service()->name,
                      my_instance->get_service()->ports ? "" : ": Service has no network listeners");
            return NULL;
        }
    }

    TeeSession* tee = new(std::nothrow) TeeSession(session, client, match, md_match, exclude, md_exclude);

    if (!tee)
    {
        pcre2_match_data_free(md_match);
        pcre2_match_data_free(md_exclude);

        if (client)
        {
            delete client;
        }
    }

    return tee;
}

TeeSession::~TeeSession()
{
    delete m_client;
}

void TeeSession::close()
{
}

int TeeSession::routeQuery(GWBUF* queue)
{
    if (m_client && query_matches(queue))
    {
        m_client->queue_query(queue);
    }

    return mxs::FilterSession::routeQuery(queue);
}

void TeeSession::diagnostics(DCB* pDcb)
{
}

json_t* TeeSession::diagnostics_json() const
{
    return NULL;
}

bool TeeSession::query_matches(GWBUF* buffer)
{
    bool rval = true;

    if (m_match || m_exclude)
    {
        char* sql;
        int len;

        if (modutil_extract_SQL(buffer, &sql, &len))
        {
            if (m_match && pcre2_match_8(m_match,
                                         (PCRE2_SPTR)sql,
                                         len,
                                         0,
                                         0,
                                         m_md_match,
                                         NULL) < 0)
            {
                MXS_INFO("Query does not match the 'match' pattern: %.*s", len, sql);
                rval = false;
            }
            else if (m_exclude && pcre2_match_8(m_exclude,
                                                (PCRE2_SPTR)sql,
                                                len,
                                                0,
                                                0,
                                                m_md_exclude,
                                                NULL) >= 0)
            {
                MXS_INFO("Query matches the 'exclude' pattern: %.*s", len, sql);
                rval = false;
            }
        }
    }

    return rval;
}
