/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rwsplitsession.hh"

/**
 * @bref discard the result of MASTER_GTID_WAIT statement
 *
 * The result will be an error or an OK packet.
 *
 * @param buffer Original reply buffer
 *
 * @return Any data after the ERR/OK packet, NULL for no data
 */
GWBUF* RWSplitSession::discard_master_wait_gtid_result(GWBUF* buffer)
{
    uint8_t header_and_command[MYSQL_HEADER_LEN + 1];
    gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN + 1, header_and_command);

    if (MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_OK)
    {
        // MASTER_WAIT_GTID is complete, discard the OK packet or return the ERR packet
        m_wait_gtid = UPDATING_PACKETS;

        // Discard the OK packet and start updating sequence numbers
        uint8_t packet_len = MYSQL_GET_PAYLOAD_LEN(header_and_command) + MYSQL_HEADER_LEN;
        m_next_seq         = 1;
        buffer             = gwbuf_consume(buffer, packet_len);
    }
    else if (MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_ERR)
    {
        if (trx_is_read_only())
        {
            // If a causal read fails inside of a read-only transaction, it cannot be retried on the master.
            m_wait_gtid = NONE;
            gwbuf_free(buffer);
            buffer = modutil_create_mysql_err_msg(0,
                0,
                1792,
                "25006",
                "Causal read timed out while in a read-only transaction, cannot retry command.");
        }
        else
        {
            // The MASTER_WAIT_GTID command failed and no further packets will come
            m_wait_gtid = RETRYING_ON_MASTER;
        }
    }

    return buffer;
}

/**
 * @bref After discarded the wait result, we need correct the seqence number of every packet
 *
 * @param buffer origin reply buffer
 *
 */
void RWSplitSession::correct_packet_sequence(GWBUF* buffer)
{
    uint8_t header[3];
    uint32_t offset = 0;

    while (gwbuf_copy_data(buffer, offset, 3, header) == 3)
    {
        uint32_t packet_len = MYSQL_GET_PAYLOAD_LEN(header) + MYSQL_HEADER_LEN;
        uint8_t* seq        = gwbuf_byte_pointer(buffer, offset + MYSQL_SEQ_OFFSET);
        *seq                = m_next_seq++;
        offset += packet_len;
    }
}

GWBUF* RWSplitSession::handle_causal_read_reply(
    GWBUF* writebuf, const mxs::Reply& reply, mxs::RWBackend* backend)
{
    if (m_config.causal_reads != CausalReads::NONE)
    {
        if (reply.is_ok() && backend == m_current_master)
        {
            auto gtid = reply.get_variable(MXS_LAST_GTID);

            if (!gtid.empty())
            {
                if (m_config.causal_reads == CausalReads::GLOBAL)
                {
                    m_router->set_last_gtid(gtid);
                }
                else
                {
                    m_gtid_pos = RWSplit::gtid::from_string(gtid);
                }
            }
        }

        if (m_wait_gtid == WAITING_FOR_HEADER)
        {
            mxb_assert(m_prev_target == backend);
            writebuf = discard_master_wait_gtid_result(writebuf);
        }

        if (m_wait_gtid == UPDATING_PACKETS && writebuf)
        {
            mxb_assert(m_prev_target == backend);
            correct_packet_sequence(writebuf);
        }
    }

    return writebuf;
}

bool RWSplitSession::finish_causal_read()
{
    bool rval = true;

    if (m_config.causal_reads != CausalReads::NONE)
    {
        if (m_wait_gtid == RETRYING_ON_MASTER)
        {
            // Retry the query on the master
            GWBUF* buf = m_current_query.release();
            buf->hint  = hint_create_route(buf->hint, HINT_ROUTE_TO_MASTER, NULL);
            retry_query(buf, 0);
            rval = false;
        }

        // The reply should never be complete while we are still waiting for the header.
        mxb_assert(m_wait_gtid != WAITING_FOR_HEADER);
        m_wait_gtid = NONE;
    }

    return rval;
}

/*
 * Add a wait gitd query in front of user's query to achive causal read
 *
 * @param version Server version
 * @param origin  Original buffer
 *
 * @return       A new buffer contains wait statement and origin query
 */
GWBUF* RWSplitSession::add_prefix_wait_gtid(uint64_t version, GWBUF* origin)
{
    /**
     * Pack wait function and client query into a multistatments will save a round trip latency,
     * and prevent the client query being executed on timeout.
     * For example:
     * SET @maxscale_secret_variable=(SELECT CASE WHEN MASTER_GTID_WAIT('232-1-1', 10) = 0
     * THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END); SELECT * FROM `city`;
     * when MASTER_GTID_WAIT('232-1-1', 0.05) == 1 (timeout), it will return
     * an error, and SELECT * FROM `city` will not be executed, then we can retry
     * on master;
     **/

    GWBUF* rval = origin;

    // If the version is not known, assume MariaDB backend
    const char* wait_func
        = (version > 50700 && version < 100000) ? MYSQL_WAIT_GTID_FUNC : MARIADB_WAIT_GTID_FUNC;

    const char* gtid_wait_timeout = m_config.causal_reads_timeout.c_str();
    std::string gtid_position
        = m_config.causal_reads == CausalReads::GLOBAL ? m_router->last_gtid() : m_gtid_pos.to_string();

    /* Create a new buffer to store prefix sql */
    size_t prefix_len
        = strlen(gtid_wait_stmt) + gtid_position.length() + strlen(gtid_wait_timeout) + strlen(wait_func);

    // Only do the replacement if it fits into one packet
    if (gwbuf_length(origin) + prefix_len < GW_MYSQL_MAX_PACKET_LEN + MYSQL_HEADER_LEN)
    {
        char prefix_sql[prefix_len];
        snprintf(prefix_sql, prefix_len, gtid_wait_stmt, wait_func, gtid_position.c_str(), gtid_wait_timeout);
        GWBUF* prefix_buff = modutil_create_query(prefix_sql);

        // Copy the original query in case it fails on the slave
        m_current_query.copy_from(origin);

        /* Trim origin to sql, Append origin buffer to the prefix buffer */
        uint8_t header[MYSQL_HEADER_LEN];
        gwbuf_copy_data(origin, 0, MYSQL_HEADER_LEN, header);
        /* Command length = 1 */
        size_t origin_sql_len = MYSQL_GET_PAYLOAD_LEN(header) - 1;
        /* Trim mysql header and command */
        origin = gwbuf_consume(origin, MYSQL_HEADER_LEN + 1);
        rval   = gwbuf_append(prefix_buff, origin);

        /* Modify totol length: Prefix sql len + origin sql len + command len */
        size_t new_payload_len = strlen(prefix_sql) + origin_sql_len + 1;
        gw_mysql_set_byte3(GWBUF_DATA(rval), new_payload_len);
    }

    return rval;
}
