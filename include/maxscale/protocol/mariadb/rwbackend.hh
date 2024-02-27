/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxscale/backend.hh>
#include <maxscale/response_stat.hh>

#include <map>
#include <vector>

using Endpoints = std::vector<mxs::Endpoint*>;

namespace maxscale
{

typedef std::map<uint32_t, uint32_t> BackendHandleMap;      /** Internal ID to external ID */

class RWBackend;

// All interfacing is now handled via RWBackend*.
using PRWBackends = std::vector<RWBackend*>;

// Internal storage for a class containing RWBackend:s.
using RWBackends = std::vector<RWBackend>;

class RWBackend : public mxs::Backend
{
public:
    RWBackend(const RWBackend&) = delete;
    RWBackend& operator=(const RWBackend&) = delete;
    RWBackend(RWBackend&&) = delete;
    RWBackend& operator=(RWBackend&&) = delete;

    static RWBackends from_endpoints(const Endpoints& endpoints);

    RWBackend(mxs::Endpoint* endpoint);
    virtual ~RWBackend() = default;

    void select_started() override;
    void select_finished() override;

    /**
     * Write a query to the backend
     *
     * This function handles the replacement of the prepared statement IDs from
     * the internal ID to the server specific one. Trailing parts of large
     * packets should use RWBackend::continue_write.
     *
     * @param buffer Buffer to write
     * @param type   Whether a response is expected
     *
     * @return True if writing was successful
     */
    bool write(GWBUF&& buffer, response_type type = EXPECT_RESPONSE) override;

    void close(close_type type = CLOSE_NORMAL) override;

    maxbase::TimePoint last_write() const
    {
        return m_last_write;
    }

    void sync_averages();

private:
    ResponseStat m_response_stat;
    bool         m_large_query = false;

    maxbase::TimePoint m_last_write;
};
}
