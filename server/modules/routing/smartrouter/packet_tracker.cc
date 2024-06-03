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

#include "packet_tracker.hh"
#include <maxbase/log.hh>
#include <sstream>

namespace maxsql
{

PacketTracker::PacketTracker(const GWBUF& packet)
{
    uint8_t command = mariadb::get_command(packet);
    m_multipart.track_query(packet);
    m_expecting_response = mariadb::command_will_respond(command);

    MXB_SDEBUG("PacketTracker Command: " << mariadb::cmd_to_string(command));
}

void PacketTracker::update_request(const GWBUF& packet)
{
    MXB_SDEBUG("PacketTracker update_request");
    mxb_assert_message(m_multipart.is_multipart() || m_multipart.is_ldli(),
                       "PacketTracker::update_request() called while not expecting splits");

    m_multipart.track_query(packet);

    mxb_assert_message(m_multipart.should_ignore(),
                       "PacketTracker::update_request() received a non-split packet");
}

bool PacketTracker::expecting_request_packets() const
{
    return m_multipart.should_ignore();
}

bool PacketTracker::expecting_response_packets() const
{
    return m_expecting_response;
}

bool PacketTracker::expecting_more_packets() const
{
    return expecting_response_packets() || expecting_request_packets();
}

void PacketTracker::update_response(const mxs::Reply& reply)
{
    m_multipart.track_reply(reply);
    m_expecting_response = !reply.is_complete();
}
}
