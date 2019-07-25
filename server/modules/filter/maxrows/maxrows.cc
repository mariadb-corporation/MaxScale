/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file maxrows.c - Result set limit Filter
 */

#include "maxrows.hh"

#include <stdbool.h>
#include <stdint.h>

#include <maxbase/assert.h>
#include <maxbase/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/paths.h>
#include <maxscale/poll.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/query_classifier.hh>

void maxrows_response_state_reset(MAXROWS_RESPONSE_STATE* state)
{
    memset(state, 0, sizeof(*state));
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param buffer    Buffer containing an MySQL protocol packet.
 */
int MaxRowsSession::old_routeQuery(GWBUF* packet)
{
    MaxRows* cinstance = instance;
    MaxRowsSession* csdata = this;

    uint8_t* data = GWBUF_DATA(packet);

    // All of these should be guaranteed by RCAP_TYPE_TRANSACTION_TRACKING
    mxb_assert(GWBUF_IS_CONTIGUOUS(packet));
    mxb_assert(GWBUF_LENGTH(packet) >= MYSQL_HEADER_LEN + 1);
    mxb_assert(MYSQL_GET_PAYLOAD_LEN(data)
               + MYSQL_HEADER_LEN == GWBUF_LENGTH(packet));

    maxrows_response_state_reset(&csdata->res);
    csdata->state = MAXROWS_IGNORING_RESPONSE;
    csdata->large_packet = false;
    csdata->discard_resultset = false;
    // Set buffer size to 0
    csdata->res.length = 0;

    switch ((int)MYSQL_GET_COMMAND(data))
    {
    case MXS_COM_QUERY:
    case MXS_COM_STMT_EXECUTE:
        {
            /* Set input query only with Mode::ERR */
            if (csdata->instance->config().mode == Mode::ERR
                && (csdata->input_sql = gwbuf_clone(packet)) == NULL)
            {
                csdata->state = MAXROWS_EXPECTING_NOTHING;

                /* Abort client connection on copy failure */
                poll_fake_hangup_event(csdata->session->client_dcb);
                gwbuf_free(csdata->res.data);
                gwbuf_free(packet);
                MXS_FREE(csdata);
                csdata->res.data = NULL;
                packet = NULL;
                csdata = NULL;
                return 0;
            }

            csdata->state = MAXROWS_EXPECTING_RESPONSE;
            break;
        }

    default:
        break;
    }

    if (csdata->instance->config().debug & MAXROWS_DEBUG_DECISIONS)
    {
        MXS_NOTICE("Maxrows filter is sending data.");
    }

    return FilterSession::routeQuery(packet);
}

/**
 * A response on its way to the client is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param queue     The query data
 */
int MaxRowsSession::old_clientReply(GWBUF* data, DCB* dcb)
{
    MaxRows* cinstance = instance;
    MaxRowsSession* csdata = this;

    int rv;

    if (csdata->res.data)
    {
        if (csdata->discard_resultset
            && csdata->state == MAXROWS_EXPECTING_ROWS)
        {
            gwbuf_free(csdata->res.data);
            csdata->res.data = data;
            csdata->res.length = gwbuf_length(data);
            csdata->res.offset = 0;
        }
        else
        {
            gwbuf_append(csdata->res.data, data);
            csdata->res.length += gwbuf_length(data);
        }
    }
    else
    {
        csdata->res.data = data;
        csdata->res.length = gwbuf_length(data);
    }

    if (csdata->state != MAXROWS_IGNORING_RESPONSE)
    {
        if (!csdata->discard_resultset)
        {
            if (csdata->res.length > csdata->instance->config().max_size)
            {
                if (csdata->instance->config().debug & MAXROWS_DEBUG_DISCARDING)
                {
                    MXS_NOTICE("Current size %luB of resultset, at least as much "
                               "as maximum allowed size %uKiB. Not returning data.",
                               csdata->res.length,
                               csdata->instance->config().max_size / 1024);
                }

                csdata->discard_resultset = true;
            }
        }
    }

    switch (csdata->state)
    {
    case MAXROWS_EXPECTING_FIELDS:
        rv = handle_expecting_fields(csdata);
        break;

    case MAXROWS_EXPECTING_NOTHING:
        rv = handle_expecting_nothing(csdata);
        break;

    case MAXROWS_EXPECTING_RESPONSE:
        rv = handle_expecting_response(csdata);
        break;

    case MAXROWS_EXPECTING_ROWS:
        rv = handle_rows(csdata, data, 0);
        break;

    case MAXROWS_IGNORING_RESPONSE:
        rv = handle_ignoring_response(csdata);
        break;

    default:
        MXS_ERROR("Internal filter logic broken, unexpected state: %d",
                  csdata->state);
        mxb_assert(!true);
        rv = send_upstream(csdata);
        maxrows_response_state_reset(&csdata->res);
        csdata->state = MAXROWS_IGNORING_RESPONSE;
    }

    return rv;
}

/* API END */

/**
 * Reset maxrows response state
 *
 * @param state Pointer to object.
 */


/**
 * Called when resultset field information is handled.
 *
 * @param csdata The maxrows session data.
 */
int MaxRowsSession::handle_expecting_fields(MaxRowsSession* csdata)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_FIELDS);
    mxb_assert(csdata->res.data);

    int rv = 1;

    bool insufficient = false;

    size_t buflen = csdata->res.length;

    while (!insufficient && (buflen - csdata->res.offset >= MYSQL_HEADER_LEN))
    {
        uint8_t header[MYSQL_HEADER_LEN + 1];
        gwbuf_copy_data(csdata->res.data,
                        csdata->res.offset,
                        MYSQL_HEADER_LEN + 1,
                        header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(header);

        if (csdata->res.offset + packetlen <= buflen)
        {
            // We have at least one complete packet.
            int command = (int)MYSQL_GET_COMMAND(header);

            switch (command)
            {
            case 0xfe:      // EOF, the one after the fields.
                csdata->res.offset += packetlen;

                /**
                 * Set the buffer with column definitions.
                 * This will be used only by the empty response handler.
                 */
                if (!csdata->res.column_defs
                    && csdata->instance->config().mode == Mode::EMPTY)
                {
                    csdata->res.column_defs = gwbuf_clone(csdata->res.data);
                }

                csdata->state = MAXROWS_EXPECTING_ROWS;
                rv = handle_rows(csdata, csdata->res.data, csdata->res.offset);
                break;

            default:    // Field information.
                csdata->res.offset += packetlen;
                ++csdata->res.n_fields;
                mxb_assert(csdata->res.n_fields <= csdata->res.n_totalfields);
                break;
            }
        }
        else
        {
            // We need more data
            insufficient = true;
        }
    }

    return rv;
}

/**
 * Called when data is received (even if nothing is expected) from the server.
 *
 * @param csdata The maxrows session data.
 */
int MaxRowsSession::handle_expecting_nothing(MaxRowsSession* csdata)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_NOTHING);
    mxb_assert(csdata->res.data);
    unsigned long msg_size = gwbuf_length(csdata->res.data);

    if ((int)MYSQL_GET_COMMAND(GWBUF_DATA(csdata->res.data)) == 0xff)
    {
        /**
         * Error text message is after:
         * MYSQL_HEADER_LEN offset + status flag (1) + error code (2) +
         * 6 bytes message status = MYSQL_HEADER_LEN + 9
         */
        MXS_INFO("Error packet received from backend "
                 "(possibly a server shut down ?): [%.*s].",
                 (int)msg_size - (MYSQL_HEADER_LEN + 9),
                 GWBUF_DATA(csdata->res.data) + MYSQL_HEADER_LEN + 9);
    }
    else
    {
        MXS_WARNING("Received data from the backend although "
                    "filter is expecting nothing. "
                    "Packet size is %lu bytes long.",
                    msg_size);
        mxb_assert(!true);
    }

    return send_upstream(csdata);
}

/**
 * Called when a response is received from the server.
 *
 * @param csdata The maxrows session data.
 */
int MaxRowsSession::handle_expecting_response(MaxRowsSession* csdata)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_RESPONSE);
    mxb_assert(csdata->res.data);

    int rv = 1;
    size_t buflen = csdata->res.length;

    // Reset field counters
    csdata->res.n_fields = 0;
    csdata->res.n_totalfields = 0;
    // Reset large packet var
    csdata->large_packet = false;

    if (buflen >= MYSQL_HEADER_LEN + 1)     // We need the command byte.
    {
        // Reserve enough space to accomodate for the largest length encoded integer,
        // which is type field + 8 bytes.
        uint8_t header[MYSQL_HEADER_LEN + 1 + 8];

        // Read packet header from buffer at current offset
        gwbuf_copy_data(csdata->res.data,
                        csdata->res.offset,
                        MYSQL_HEADER_LEN + 1,
                        header);

        switch ((int)MYSQL_GET_COMMAND(header))
        {
        case 0x00:      // OK
        case 0xff:      // ERR
            /**
             * This also handles the OK packet that terminates
             * a Multi-Resultset seen in handle_rows()
             */
            if (csdata->instance->config().debug & MAXROWS_DEBUG_DECISIONS)
            {
                if (csdata->res.n_rows)
                {
                    MXS_NOTICE("OK or ERR seen. The resultset has %lu rows.%s",
                               csdata->res.n_rows,
                               csdata->discard_resultset ? " [Discarded]" : "");
                }
                else
                {
                    MXS_NOTICE("OK or ERR");
                }
            }

            if (csdata->discard_resultset)
            {
                rv = send_maxrows_reply_limit(csdata);
                csdata->state = MAXROWS_EXPECTING_NOTHING;
            }
            else
            {
                rv = send_upstream(csdata);
                csdata->state = MAXROWS_IGNORING_RESPONSE;
            }
            break;

        case 0xfb:      // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
            if (csdata->instance->config().debug & MAXROWS_DEBUG_DECISIONS)
            {
                MXS_NOTICE("GET_MORE_CLIENT_DATA");
            }
            rv = send_upstream(csdata);
            csdata->state = MAXROWS_IGNORING_RESPONSE;
            break;

        default:
            if (csdata->instance->config().debug & MAXROWS_DEBUG_DECISIONS)
            {
                MXS_NOTICE("RESULTSET");
            }

            if (csdata->res.n_totalfields != 0)
            {
                // We've seen the header and have figured out how many fields there are.
                csdata->state = MAXROWS_EXPECTING_FIELDS;
                rv = handle_expecting_fields(csdata);
            }
            else
            {
                // mxs_leint_bytes() returns the length of the int type field + the size of the
                // integer.
                size_t n_bytes = mxq::leint_bytes(&header[4]);

                if (MYSQL_HEADER_LEN + n_bytes <= buflen)
                {
                    // Now we can figure out how many fields there are, but first we
                    // need to copy some more data.
                    gwbuf_copy_data(csdata->res.data,
                                    MYSQL_HEADER_LEN + 1,
                                    n_bytes - 1,
                                    &header[MYSQL_HEADER_LEN + 1]);

                    csdata->res.n_totalfields = mxq::leint_value(&header[4]);
                    csdata->res.offset += MYSQL_HEADER_LEN + n_bytes;

                    csdata->state = MAXROWS_EXPECTING_FIELDS;
                    rv = handle_expecting_fields(csdata);
                }
                else
                {
                    // We need more data. We will be called again, when data is available.
                }
            }
            break;
        }
    }

    return rv;
}

/**
 * Called when resultset rows are handled
 *
 * @param csdata       The maxrows session data
 * @param buffer       The buffer containing the packet
 * @param extra_offset Offset into @c buffer where the packet is stored
 *
 * @return The return value of the upstream component
 */
int MaxRowsSession::handle_rows(MaxRowsSession* csdata, GWBUF* buffer, size_t extra_offset)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_ROWS);
    mxb_assert(csdata->res.data);

    int rv = 1;
    bool insufficient = false;
    size_t offset = extra_offset;
    size_t buflen = gwbuf_length(buffer);

    while (!insufficient && (buflen - offset >= MYSQL_HEADER_LEN))
    {
        bool pending_large_data = csdata->large_packet;
        // header array holds a full EOF packet
        uint8_t header[MYSQL_EOF_PACKET_LEN];
        gwbuf_copy_data(buffer,
                        offset,
                        MYSQL_EOF_PACKET_LEN,
                        header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(header);

        if (offset + packetlen <= buflen)
        {
            /* Check for large packet packet terminator:
             * min is 4 bytes "0x0 0x0 0x0 0xseq_no and
             * max is 1 byte less than EOF_PACKET_LEN
             * If true skip data processing.
             */
            if (pending_large_data
                && (packetlen >= MYSQL_HEADER_LEN
                    && packetlen < MYSQL_EOF_PACKET_LEN))
            {
                // Update offset, number of rows and break
                offset += packetlen;
                csdata->res.n_rows++;
                break;
            }

            /*
             * Check packet size against MYSQL_PACKET_LENGTH_MAX
             * If true then break as received could be not complete
             * EOF or OK packet could be seen after receiving the full large packet
             */
            if (packetlen == (MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN))
            {
                // Mark the beginning of a large packet receiving
                csdata->large_packet = true;
                // Just update offset and break
                offset += packetlen;
                break;
            }
            else
            {
                // Reset large packet indicator
                csdata->large_packet = false;
            }

            // We have at least one complete packet and we can process the command byte.
            int command = (int)MYSQL_GET_COMMAND(header);
            int flags = 0;

            switch (command)
            {
            case 0xff:      // ERR packet after the rows.
                offset += packetlen;

                // This is the end of resultset: set big packet var to false
                csdata->large_packet = false;

                if (csdata->instance->config().debug & MAXROWS_DEBUG_DECISIONS)
                {
                    MXS_NOTICE("Error packet seen while handling result set");
                }

                /*
                 * This is the ERR packet that could terminate a Multi-Resultset.
                 */

                // Send data in buffer or empty resultset
                if (csdata->discard_resultset)
                {
                    rv = send_maxrows_reply_limit(csdata);
                }
                else
                {
                    rv = send_upstream(csdata);
                }

                csdata->state = MAXROWS_EXPECTING_NOTHING;

                break;

            /* OK could the last packet in the Multi-Resultset transmission:
             * this is handled by handle_expecting_response()
             *
             * It could also be sent instead of EOF from as in MySQL 5.7.5
             * if client sends CLIENT_DEPRECATE_EOF capability OK packet could
             * have the SERVER_MORE_RESULTS_EXIST flag.
             * Flags in the OK packet are at the same offset as in EOF.
             *
             * NOTE: not supported right now
             */
            case 0xfe:      // EOF, the one after the rows.
                offset += packetlen;

                /* EOF could be the last packet in the transmission:
                 * check first whether SERVER_MORE_RESULTS_EXIST flag is set.
                 * If so more results set could come. The end of stream
                 * will be an OK packet.
                 */
                if (packetlen < MYSQL_EOF_PACKET_LEN)
                {
                    MXS_ERROR("EOF packet has size of %lu instead of %d",
                              packetlen,
                              MYSQL_EOF_PACKET_LEN);
                    rv = send_maxrows_reply_limit(csdata);
                    csdata->state = MAXROWS_EXPECTING_NOTHING;
                    break;
                }

                flags = gw_mysql_get_byte2(header + MAXROWS_MYSQL_EOF_PACKET_FLAGS_OFFSET);

                // Check whether the EOF terminates the resultset or indicates MORE_RESULTS
                if (!(flags & SERVER_MORE_RESULTS_EXIST))
                {
                    // End of the resultset
                    if (csdata->instance->config().debug & MAXROWS_DEBUG_DECISIONS)
                    {
                        MXS_NOTICE("OK or EOF packet seen: the resultset has %lu rows.%s",
                                   csdata->res.n_rows,
                                   csdata->discard_resultset ? " [Discarded]" : "");
                    }

                    // Discard data or send data
                    if (csdata->discard_resultset)
                    {
                        rv = send_maxrows_reply_limit(csdata);
                    }
                    else
                    {
                        rv = send_upstream(csdata);
                    }

                    csdata->state = MAXROWS_EXPECTING_NOTHING;
                }
                else
                {
                    /*
                     * SERVER_MORE_RESULTS_EXIST flag is present: additional resultsets will come.
                     *
                     * Note: the OK packet that terminates the Multi-Resultset
                     * is handled by handle_expecting_response()
                     */

                    csdata->state = MAXROWS_EXPECTING_RESPONSE;

                    if (csdata->instance->config().debug & MAXROWS_DEBUG_DECISIONS)
                    {
                        MXS_NOTICE("EOF or OK packet seen with SERVER_MORE_RESULTS_EXIST flag:"
                                   " waiting for more data (%lu rows so far)",
                                   csdata->res.n_rows);
                    }
                }

                break;

            case 0xfb:  // NULL
            default:    // length-encoded-string
                offset += packetlen;
                // Increase res.n_rows counter while not receiving large packets
                if (!csdata->large_packet)
                {
                    csdata->res.n_rows++;
                }

                // Check for max_resultset_rows limit
                if (!csdata->discard_resultset)
                {
                    if (csdata->res.n_rows > csdata->instance->config().max_rows)
                    {
                        if (csdata->instance->config().debug & MAXROWS_DEBUG_DISCARDING)
                        {
                            MXS_INFO("max_resultset_rows %lu reached, not returning the resultset.",
                                     csdata->res.n_rows);
                        }

                        // Set the discard indicator
                        csdata->discard_resultset = true;
                    }
                }
                break;
            }
        }
        else
        {
            // We need more data
            insufficient = true;
        }
    }

    csdata->res.offset += offset - extra_offset;

    return rv;
}

/**
 * Called when all data from the server is ignored.
 *
 * @param csdata The maxrows session data.
 */
int MaxRowsSession::handle_ignoring_response(MaxRowsSession* csdata)
{
    mxb_assert(csdata->state == MAXROWS_IGNORING_RESPONSE);
    mxb_assert(csdata->res.data);

    return send_upstream(csdata);
}

/**
 * Send data upstream.
 *
 * @param csdata Session data
 *
 * @return Whatever the upstream returns.
 */
int MaxRowsSession::send_upstream(MaxRowsSession* csdata)
{
    mxb_assert(csdata->res.data != NULL);

    /* Free a saved SQL not freed by send_error_upstream() */
    if (csdata->instance->config().mode == Mode::ERR)
    {
        gwbuf_free(csdata->input_sql);
        csdata->input_sql = NULL;
    }

    /* Free a saved columndefs not freed by send_eof_upstream() */
    if (csdata->instance->config().mode == Mode::EMPTY)
    {
        gwbuf_free(csdata->res.column_defs);
        csdata->res.column_defs = NULL;
    }

    // TODO: Fix this

    /* Send data to client */
    int rv = FilterSession::clientReply(csdata->res.data, nullptr);
    csdata->res.data = NULL;

    return rv;
}

/**
 * Send upstream the Response Buffer up to columns def in response
 * including its EOF of the first result set
 * An EOF packet for empty result set with no MULTI flags is added
 * at the end.
 *
 * @param csdata    Session data
 *
 * @return          Non-Zero if successful, 0 on errors
 */
int MaxRowsSession::send_eof_upstream(MaxRowsSession* csdata)
{
    int rv = -1;
    /* Sequence byte is #3 */
    uint8_t eof[MYSQL_EOF_PACKET_LEN] = {05, 00, 00, 01, 0xfe, 00, 00, 02, 00};
    GWBUF* new_pkt = NULL;

    mxb_assert(csdata->res.data != NULL);
    mxb_assert(csdata->res.column_defs != NULL);

    /**
     * The offset to server reply pointing to
     * next byte after column definitions EOF
     * of the first result set.
     */
    size_t offset = gwbuf_length(csdata->res.column_defs);

    /* Data to send + added EOF */
    uint8_t* new_result = static_cast<uint8_t*>(MXS_MALLOC(offset + MYSQL_EOF_PACKET_LEN));

    if (new_result)
    {
        /* Get contiguous data from saved columns defintions buffer */
        gwbuf_copy_data(csdata->res.column_defs, 0, offset, new_result);

        /* Increment sequence number for the EOF being added for empty resultset:
         * last one if found in EOF terminating column def
         */
        eof[3] = new_result[offset - (MYSQL_EOF_PACKET_LEN - 3)] + 1;

        /* Copy EOF data */
        memcpy(new_result + offset, &eof, MYSQL_EOF_PACKET_LEN);

        /* Create new packet */
        new_pkt = gwbuf_alloc_and_load(offset + MYSQL_EOF_PACKET_LEN, new_result);

        /* Free intermediate data */
        MXS_FREE(new_result);

        if (new_pkt)
        {
            // TODO: Fix this
            rv = FilterSession::clientReply(new_pkt, nullptr);
        }
    }

    /* Abort client connection */
    if (!(new_result && new_pkt))
    {
        /* Abort client connection */
        poll_fake_hangup_event(csdata->session->client_dcb);
        rv = 0;
    }

    /* Free all data buffers */
    gwbuf_free(csdata->res.data);
    gwbuf_free(csdata->res.column_defs);

    csdata->res.data = NULL;
    csdata->res.column_defs = NULL;

    return rv;
}

/**
 * Send OK packet data upstream.
 *
 * @param csdata    Session data
 *
 * @return          Non-Zero if successful, 0 on errors
 */
int MaxRowsSession::send_ok_upstream(MaxRowsSession* csdata)
{
    /* Note: sequence id is always 01 (4th byte) */
    const static uint8_t ok[MYSQL_OK_PACKET_MIN_LEN] = {07, 00, 00, 01, 00, 00,
                                                        00, 02, 00, 00, 00};

    mxb_assert(csdata->res.data != NULL);

    GWBUF* packet = gwbuf_alloc(MYSQL_OK_PACKET_MIN_LEN);
    if (!packet)
    {
        /* Abort clienrt connection */
        poll_fake_hangup_event(csdata->session->client_dcb);
        gwbuf_free(csdata->res.data);
        csdata->res.data = NULL;
        return 0;
    }

    uint8_t* ptr = GWBUF_DATA(packet);
    memcpy(ptr, &ok, MYSQL_OK_PACKET_MIN_LEN);

    mxb_assert(csdata->res.data != NULL);

    // TODO: Fix this
    int rv = FilterSession::clientReply(packet, nullptr);

    /* Free server result buffer */
    gwbuf_free(csdata->res.data);
    csdata->res.data = NULL;

    return rv;
}

/**
 * Send ERR packet data upstream.
 *
 * An error packet is sent to client including
 * a message prefix plus the original SQL input
 *
 * @param   csdata    Session data
 * @return            Non-Zero if successful, 0 on errors
 */
int MaxRowsSession::send_error_upstream(MaxRowsSession* csdata)
{
    GWBUF* err_pkt;
    uint8_t hdr_err[MYSQL_ERR_PACKET_MIN_LEN];
    unsigned long bytes_copied;
    const char* err_msg_prefix = "Row limit/size exceeded for query: ";
    int err_prefix_len = strlen(err_msg_prefix);
    unsigned long pkt_len = MYSQL_ERR_PACKET_MIN_LEN + err_prefix_len;
    unsigned long sql_len = gwbuf_length(csdata->input_sql)
        - (MYSQL_HEADER_LEN + 1);
    /**
     * The input SQL statement added in the error message
     * has a limit of MAXROWS_INPUT_SQL_MAX_LEN bytes
     */
    sql_len = (sql_len > MAXROWS_INPUT_SQL_MAX_LEN) ?
        MAXROWS_INPUT_SQL_MAX_LEN : sql_len;
    uint8_t sql[sql_len];

    mxb_assert(csdata->res.data != NULL);

    pkt_len += sql_len;

    bytes_copied = gwbuf_copy_data(csdata->input_sql,
                                   MYSQL_HEADER_LEN + 1,
                                   sql_len,
                                   sql);

    if (!bytes_copied
        || (err_pkt = gwbuf_alloc(MYSQL_HEADER_LEN + pkt_len)) == NULL)
    {
        /* Abort client connection */
        poll_fake_hangup_event(csdata->session->client_dcb);
        gwbuf_free(csdata->res.data);
        gwbuf_free(csdata->input_sql);
        csdata->res.data = NULL;
        csdata->input_sql = NULL;

        return 0;
    }

    uint8_t* ptr = GWBUF_DATA(err_pkt);
    memcpy(ptr, &hdr_err, MYSQL_ERR_PACKET_MIN_LEN);
    unsigned int err_errno = 1415;
    char err_state[7] = "#0A000";

    /* Set the payload length of the whole error message */
    gw_mysql_set_byte3(&ptr[0], pkt_len);
    /* Note: sequence id is always 01 (4th byte) */
    ptr[3] = 1;
    /* Error indicator */
    ptr[4] = 0xff;
    /* MySQL error code: 2 bytes */
    gw_mysql_set_byte2(&ptr[5], err_errno);
    /* Status Message 6 bytes */
    memcpy((char*)&ptr[7], err_state, 6);
    /* Copy error message prefix */
    memcpy(&ptr[13], err_msg_prefix, err_prefix_len);
    /* Copy SQL input */
    memcpy(&ptr[13 + err_prefix_len], sql, sql_len);

    // TODO: Fix this
    int rv = FilterSession::clientReply(err_pkt, nullptr);

    /* Free server result buffer */
    gwbuf_free(csdata->res.data);
    csdata->res.data = NULL;

    /* Free input_sql buffer */
    gwbuf_free(csdata->input_sql);
    csdata->input_sql = NULL;

    return rv;
}

/**
 * Send the proper reply to client when the maxrows
 * limit/size is hit.
 *
 * @param   csdata    Session data
 * @return            Non-Zero if successful, 0 on errors
 */
int MaxRowsSession::send_maxrows_reply_limit(MaxRowsSession* csdata)
{
    switch (csdata->instance->config().mode)
    {
    case Mode::EMPTY:
        return send_eof_upstream(csdata);
        break;

    case Mode::OK:
        return send_ok_upstream(csdata);
        break;

    case Mode::ERR:
        return send_error_upstream(csdata);
        break;

    default:
        MXS_ERROR("MaxRows config value not expected!");
        mxb_assert(!true);
        return 0;
        break;
    }
}

int MaxRowsSession::routeQuery(GWBUF* packet)
{
    return FilterSession::routeQuery(packet);
}

int MaxRowsSession::clientReply(GWBUF* data, DCB* dcb)
{
    mxs::Buffer buffer(data);
    MySQLProtocol::Reply reply = static_cast<MySQLProtocol*>(dcb->protocol)->reply();
    int rv = 1;

    if (m_collect)
    {
        // The resultset is stored in an internal buffer until we know whether to send it or to discard it
        m_buffer.append(buffer.release());

        if (reply.rows_read() > m_instance->config().max_rows || reply.size() > m_instance->config().max_size)
        {
            // A limit was exceeded, discard the result and replace it with a fake result
            switch (m_instance->config().mode)
            {
            case Mode::EMPTY:
                if (reply.rows_read() > 0)
                {
                    // We have the start of the resultset with at least one row in it. Truncate the result
                    // to contain the start of the first resultset with no rows and inject an EOF packet into
                    // it.
                    uint64_t num_packets = reply.field_counts()[0] + 2;
                    auto tmp = mxs::truncate_packets(m_buffer.release(), num_packets);
                    m_buffer.append(tmp);
                    m_buffer.append(modutil_create_eof(num_packets + 1));
                    m_collect = false;
                }
                break;

            case Mode::ERR:
                m_buffer.reset(
                    modutil_create_mysql_err_msg(1, 0, 1226, "42000",
                                                 reply.rows_read() > m_instance->config().max_rows ?
                                                 "Resultset row limit exceeded" :
                                                 "Resultset size limit exceeded"));
                m_collect = false;
                break;

            case Mode::OK:
                m_buffer.reset(modutil_create_ok());
                m_collect = false;
                break;

            default:
                mxb_assert(!true);
                rv = 0;
                break;
            }
        }
    }

    if (reply.is_complete())
    {
        rv = FilterSession::clientReply(m_buffer.release(), dcb);
        m_collect = true;
    }

    return rv;
}

/**
 * The module entry point function, called when the module is loaded.
 *
 * @return The module object.
 */
extern "C"
{
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A filter that limits resultsets.",
        "V1.0.0",
        MaxRows::CAPABILITIES,
        &MaxRows::s_object,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {
                "max_resultset_rows",
                MXS_MODULE_PARAM_COUNT,
                MXS_MODULE_PARAM_COUNT_MAX
            },
            {
                "max_resultset_size",
                MXS_MODULE_PARAM_SIZE,
                "65536"
            },
            {
                "debug",
                MXS_MODULE_PARAM_COUNT,
                "0",
                MXS_MODULE_OPT_DEPRECATED
            },
            {
                "max_resultset_return",
                MXS_MODULE_PARAM_ENUM,
                "empty",
                MXS_MODULE_OPT_ENUM_UNIQUE,
                mode_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}
