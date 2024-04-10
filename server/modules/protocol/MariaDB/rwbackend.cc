/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/protocol/mariadb/rwbackend.hh>

#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/router.hh>

using std::chrono::seconds;

namespace maxscale
{

RWBackend::RWBackend(mxs::Endpoint* ref)
    : mxs::Backend(ref)
    , m_response_stat(target(), 9, std::chrono::milliseconds(250))
    , m_last_write(maxbase::Clock::now(maxbase::NowType::EPollTick))
{
}

bool RWBackend::write(GWBUF&& buffer, response_type type)
{
    m_last_write = maxbase::Clock::now(maxbase::NowType::EPollTick);
    uint32_t len = mariadb::get_packet_length(buffer.data());
    bool was_large_query = m_large_query;
    m_large_query = len == MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN;

    if (was_large_query)
    {
        type = Backend::NO_RESPONSE;
    }

    return mxs::Backend::write(std::move(buffer), type);
}

void RWBackend::close(close_type type)
{
    mxs::Backend::close(type);
}

void RWBackend::sync_averages()
{
    m_response_stat.sync();
}

mxs::RWBackends RWBackend::from_endpoints(const Endpoints& endpoints)
{
    return RWBackends{endpoints.begin(), endpoints.end()};
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
