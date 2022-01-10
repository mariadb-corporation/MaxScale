/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/rwbackend.hh>

#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/router.hh>

using Iter = mxs::Buffer::iterator;
using std::chrono::seconds;

namespace maxscale
{

RWBackend::RWBackend(mxs::Endpoint* ref)
    : mxs::Backend(ref)
    , m_response_stat(target(), 9, std::chrono::milliseconds(250))
    , m_last_write(maxbase::Clock::now(maxbase::NowType::EPollTick))
{
}

bool RWBackend::write(GWBUF* buffer, response_type type)
{
    m_last_write = maxbase::Clock::now(maxbase::NowType::EPollTick);
    uint32_t len = mxs_mysql_get_packet_len(buffer);
    bool was_large_query = m_large_query;
    m_large_query = len == MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN;

    if (was_large_query)
    {
        return mxs::Backend::write(buffer, Backend::NO_RESPONSE);
    }

    return mxs::Backend::write(buffer, type);
}

void RWBackend::close(close_type type)
{
    mxs::Backend::close(type);
}

void RWBackend::sync_averages()
{
    m_response_stat.sync();
}

mxs::SRWBackends RWBackend::from_endpoints(const Endpoints& endpoints)
{
    SRWBackends backends;
    backends.reserve(endpoints.size());

    for (auto e : endpoints)
    {
        backends.emplace_back(new mxs::RWBackend(e));
    }

    return backends;
}

void RWBackend::select_started()
{
    Backend::select_started();
    m_response_stat.query_started();
}

void RWBackend::select_finished()
{
    Backend::select_finished();

    m_response_stat.query_finished();
}
}
