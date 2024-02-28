/*
 * Copyright (c) 2024 MariaDB plc
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
#include <maxscale/buffer.hh>
#include <maxscale/target.hh>
#include <maxsimd/canonical.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#include <map>
#include <deque>

namespace mariadb
{
/**
 * A class that follows whether the packet given to routeQuery is the start of a packet or a trailing piece of
 * a multi-part packet. It also tracks whether a LOAD DATA LOCAL INFILE is ongoing during which all routed
 * packets are to be handled as raw data.
 */
class MultiPartTracker
{
public:
    void track_query(const GWBUF& buffer)
    {
        m_large = m_next_large;
        m_next_large = mariadb::get_header(buffer.data()).pl_length == GW_MYSQL_MAX_PACKET_LEN;
    }

    void track_reply(const mxs::Reply& reply)
    {
        m_ldli = reply.state() == mxs::ReplyState::LOAD_DATA;
    }

    bool should_ignore() const
    {
        return m_ldli || m_large;
    }

    bool m_large {false};
    bool m_next_large {false};
    bool m_ldli {false};
};

/**
 * A class that uses a COM_STMT_PREPARE as a template and uses the values from a COM_STMT_EXECUTE to form a
 * text SQL query. This can be used to log the binary protocol commands as if they were text protocol
 * commands.
 */
class PsTracker final : public MultiPartTracker
{
public:
    /**
     * Track a query
     *
     * This function should be called in routeQuery for every new packet that is routed.
     *
     * @param buffer The buffer being routed
     */
    void track_query(const GWBUF& buffer);

    /**
     * Track a reply
     *
     * This should be called in the clientReply function for all responses that are returned to the client.
     * Responses for ignored queries (e.g. session commands on non-primary backends) should not be passed to
     * this function.
     *
     * @param reply The reply to track
     */
    void track_reply(const mxs::Reply& reply);

    /**
     * Convert the given buffer into SQL
     *
     * If a COM_STMT_EXECUTE packet is given, replaces the placeholders in the corresponding COM_STMT_PREPARE
     * with the binary values of the COM_STMT_EXECUTE and returns the resulting SQL. If given a COM_QUERY,
     * returns the SQL in the packet. This behavior is simply for convenience where a new std::string is
     * always constructed from the SQL.
     *
     * @param buffer The buffer to convert
     *
     * @return The SQL command or empty string on error
     */
    std::string to_sql(const GWBUF& buffer) const;

    /**
     * Get the prepared statement and the arguments from a COM_STMT_EXECUTE
     *
     * By calling maxsimd::canonical_args_to_sql() with the return values, the original SQL string can be
     * recreated.
     *
     * @param buffer Buffer containing a COM_STMT_EXECUTE command
     *
     * @return The prepared statement and the arguments
     */
    std::pair<std::string_view, maxsimd::CanonicalArgs> get_args(const GWBUF& buffer) const;

    /**
     * Get the prepared statement for the given binary protocol command
     *
     * @param buffer Buffer containing a binary protocol command
     *
     * @return The prepared statement or an empty string on error
     */
    std::string get_prepare(const GWBUF& buffer) const;

private:
    struct Prepare
    {
        // The SQL for the prepared statement
        std::string sql;

        // The offsets into the question marks in the prepared statement. The number of parameters can be
        // deduced from it. The actual number of parameter is verified by comparing it to the COM_STMT_PREPARE
        // response.
        std::vector<uint32_t> param_offsets;

        // The type information sent in the first COM_STMT_EXECUTE packet. Subsequent executions will not send
        // it and thus it needs to be cached.
        std::vector<uint8_t> type_info;
    };

    maxsimd::CanonicalArgs convert_params_to_text(const Prepare& ps, const GWBUF& buffer) const;

    std::map<uint32_t, Prepare> m_ps;
    std::deque<GWBUF>           m_queue;
};
}
