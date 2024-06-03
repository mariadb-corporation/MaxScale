/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/trackers.hh>

namespace maxsql
{

// TODO add documentation
class PacketTracker
{
public:
    PacketTracker() = default;

    explicit PacketTracker(const GWBUF& packet);        // Track this query

    PacketTracker(const PacketTracker&) = delete;
    PacketTracker& operator=(const PacketTracker&) = delete;

    PacketTracker(PacketTracker&&) = default;
    PacketTracker& operator=(PacketTracker&&) = default;

    void update_request(const GWBUF& packet);           // Updates the query (must be a split packet)
    void update_response(const mxs::Reply& reply);      // Update as response packets are received.

    bool expecting_request_packets() const;
    bool expecting_response_packets() const;
    bool expecting_more_packets() const;

private:
    bool                      m_expecting_response = false;
    mariadb::MultiPartTracker m_multipart;
};
}
