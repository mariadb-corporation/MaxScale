/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>

#include <maxscale/backend.hh>
#include <maxscale/modutil.hh>
#include <maxscale/response_stat.hh>

using Endpoints = std::vector<mxs::Endpoint*>;

namespace maxscale
{

/** Move this somewhere else */
template<typename Smart>
std::vector<typename Smart::pointer> sptr_vec_to_ptr_vec(const std::vector<Smart>& sVec)
{
    std::vector<typename Smart::pointer> pVec;
    std::for_each(sVec.begin(), sVec.end(), [&pVec](const Smart& smart) {
                      pVec.push_back(smart.get());
                  });
    return pVec;
}

typedef std::map<uint32_t, uint32_t> BackendHandleMap;      /** Internal ID to external ID */

class RWBackend;

// All interfacing is now handled via RWBackend*.
using PRWBackends = std::vector<RWBackend*>;

// Internal storage for a class containing RWBackend:s.
using SRWBackends = std::vector<std::unique_ptr<RWBackend>>;

class RWBackend : public mxs::Backend
{
    RWBackend(const RWBackend&);
    RWBackend& operator=(const RWBackend&);

public:

    static SRWBackends from_endpoints(const Endpoints& endpoints);

    RWBackend(mxs::Endpoint* endpoint);
    virtual ~RWBackend() = default;

    bool execute_session_command();
    bool continue_session_command(GWBUF* buffer);

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
    bool write(GWBUF* buffer, response_type type = EXPECT_RESPONSE);

    void close(close_type type = CLOSE_NORMAL);

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
