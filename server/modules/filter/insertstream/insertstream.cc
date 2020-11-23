/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "insertstream"

#include <maxscale/ccdefs.hh>

#include <strings.h>
#include <maxbase/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/session.hh>

#include "insertstream.hh"

namespace
{

static uint64_t CAPS = RCAP_TYPE_TRANSACTION_TRACKING;

/**
 * This the SQL command that starts the streaming
 */
static const char load_data_template[] =
    "LOAD DATA LOCAL INFILE 'maxscale.data' INTO TABLE %s FIELDS TERMINATED BY ',' LINES TERMINATED BY '\\n'";

/**
 * Extract inserted values
 */
char* get_value(char* data, uint32_t datalen, char** dest, uint32_t* destlen)
{
    char* value_start = strnchr_esc_mysql(data, '(', datalen);

    if (value_start)
    {
        value_start++;
        char* value_end = strnchr_esc_mysql(value_start, ')', datalen - (value_start - data));

        if (value_end)
        {
            *destlen = value_end - value_start;
            *dest = value_start;
            return value_end;
        }
    }

    return NULL;
}

/**
 * @brief Convert an INSERT query into a CSV stream
 *
 * @param buffer     Buffer containing the query
 * @param packet_num The current packet sequence number
 *
 * @return The modified buffer
 */
GWBUF* convert_to_stream(GWBUF* buffer, uint8_t packet_num)
{
    /** Remove the INSERT INTO ... from the buffer */
    char* dataptr = (char*)GWBUF_DATA(buffer);
    char* modptr = strnchr_esc_mysql(dataptr + MYSQL_HEADER_LEN + 1, '(', GWBUF_LENGTH(buffer));

    /** Leave some space for the header so we don't have to allocate a new one */
    buffer = gwbuf_consume(buffer, (modptr - dataptr) - MYSQL_HEADER_LEN);
    char* header_start = (char*)GWBUF_DATA(buffer);
    char* store_end = dataptr = header_start + MYSQL_HEADER_LEN;
    char* end = static_cast<char*>(buffer->end);
    char* value;
    uint32_t valuesize;

    /**
     * Remove the parentheses from the insert, add newlines between values and
     * recalculate the packet length
     */
    while ((dataptr = get_value(dataptr, end - dataptr, &value, &valuesize)))
    {
        memmove(store_end, value, valuesize);
        store_end += valuesize;
        *store_end++ = '\n';
    }

    gwbuf_rtrim(buffer, (char*)buffer->end - store_end);
    uint32_t len = gwbuf_length(buffer) - MYSQL_HEADER_LEN;

    *header_start++ = len;
    *header_start++ = len >> 8;
    *header_start++ = len >> 16;
    *header_start = packet_num;

    return buffer;
}

/**
 * @brief Check if an insert statement has implicitly ordered values
 *
 * @param buffer Buffer to check
 *
 * @return True if the insert does not define the order of the values
 */
bool only_implicit_values(GWBUF* buffer)
{
    bool rval = false;
    char* data = (char*)GWBUF_DATA(buffer);
    char* ptr = strnchr_esc_mysql(data + MYSQL_HEADER_LEN + 1, '(', GWBUF_LENGTH(buffer));

    if (ptr && (ptr = strnchr_esc_mysql(ptr, ')', GWBUF_LENGTH(buffer) - (ptr - data))))
    {
        /** Skip the closing parenthesis and any whitespace */
        ptr++;

        while (ptr < (char*)buffer->end && isspace(*ptr))
        {
            ptr++;
        }

        if (ptr >= (char*)buffer->end || !isalnum(*ptr))
        {
            /**
             * The first pair of parentheses was followed by a non-alphanumeric
             * character. We can be fairly certain that the INSERT statement
             * implicitly defines the order of the column values as is done in
             * the following example:
             *
             * INSERT INTO test.t1 VALUES (1, "hello"), (2, "world");
             */
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Extract insert target
 *
 * @param   buffer Buffer to analyze
 *
 * @return  True if the buffer contains an insert statement and the target table
 * was successfully extracted
 */
bool extract_insert_target(GWBUF* buffer, std::string* target)
{
    bool rval = false;

    if (MYSQL_GET_COMMAND(GWBUF_DATA(buffer)) == MXS_COM_QUERY
        && qc_get_operation(buffer) == QUERY_OP_INSERT
        && only_implicit_values(buffer))
    {
        auto tables = qc_get_table_names(buffer, true);

        if (tables.size() == 1)
        {
            /** Only one table in an insert */
            *target = tables[0];
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Create a LOAD DATA LOCAL INFILE statement
 *
 * @param target The table name where the data is loaded
 *
 * @return Buffer containing the statement or NULL if memory allocation failed
 */
GWBUF* create_load_data_command(const char* target)
{
    char str[sizeof(load_data_template) + strlen(target) + 1];
    snprintf(str, sizeof(str), load_data_template, target);
    uint32_t payload = strlen(str) + 1;

    GWBUF* rval = gwbuf_alloc(payload + MYSQL_HEADER_LEN);
    if (rval)
    {
        uint8_t* ptr = GWBUF_DATA(rval);
        *ptr++ = payload;
        *ptr++ = payload >> 8;
        *ptr++ = payload >> 16;
        *ptr++ = 0;
        *ptr++ = 0x03;
        memcpy(ptr, str, payload - 1);
    }

    return rval;
}
}

InsertStream::InsertStream()
{
}

InsertStream* InsertStream::create(const char* name, mxs::ConfigParameters* params)
{
    return new InsertStream;
}

mxs::FilterSession* InsertStream::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return new InsertStreamSession(pSession, pService, this);
}

json_t* InsertStream::diagnostics() const
{
    json_t* rval = json_object();

    return rval;
}

uint64_t InsertStream::getCapabilities() const
{
    return CAPS;
}
mxs::config::Configuration* InsertStream::getConfiguration()
{
    return nullptr;
}

InsertStreamSession::InsertStreamSession(MXS_SESSION* pSession, SERVICE* pService, InsertStream* filter)
    : mxs::FilterSession(pSession, pService)
    , m_filter(filter)
{
}

int32_t InsertStreamSession::routeQuery(GWBUF* queue)
{
    std::string target;
    bool send_ok = false;
    bool send_error = false;
    int rc = 0;
    mxb_assert(gwbuf_is_contiguous(queue));

    if (m_pSession->protocol_data()->is_trx_active() && extract_insert_target(queue, &target))
    {
        switch (m_state)
        {
        case DS_STREAM_CLOSED:
            /** We're opening a new stream */
            m_target = target;
            m_queue.reset(queue);
            m_state = DS_REQUEST_SENT;
            m_packet_num = 0;
            queue = create_load_data_command(target.c_str());
            break;

        case DS_REQUEST_ACCEPTED:
            m_state = DS_STREAM_OPEN;
        /** Fallthrough */

        case DS_STREAM_OPEN:
            if (target == m_target)
            {
                /**
                 * Stream is open and targets match, convert the insert into
                 * a data stream
                 */
                uint8_t packet_num = ++m_packet_num;
                send_ok = true;
                queue = convert_to_stream(queue, packet_num);
            }
            else
            {
                /**
                 * Target mismatch
                 *
                 * TODO: Instead of sending an error, we could just open a new stream
                 */
                gwbuf_free(queue);
                send_error = true;
            }
            break;

        default:
            MXS_ERROR("Unexpected state: %d", m_state);
            mxb_assert(false);
            break;
        }
    }
    else
    {
        /** Transaction is not active or this is not an insert */
        bool send_empty = false;
        uint8_t packet_num;
        m_target.clear();

        switch (m_state)
        {
        case DS_STREAM_OPEN:
            /** Stream is open, we need to close it */
            m_state = DS_CLOSING_STREAM;
            send_empty = true;
            packet_num = ++m_packet_num;
            m_queue.reset(queue);
            break;

        case DS_REQUEST_ACCEPTED:
            m_state = DS_STREAM_OPEN;
            send_ok = true;
            break;

        default:
            mxb_assert(m_state == DS_STREAM_CLOSED);
            break;
        }

        if (send_empty)
        {
            char empty_packet[] = {0, 0, 0, static_cast<char>(packet_num)};
            queue = gwbuf_alloc_and_load(sizeof(empty_packet), &empty_packet[0]);
        }
    }

    if (send_ok)
    {
        mxs::ReplyRoute route;
        rc = FilterSession::clientReply(mxs_mysql_create_ok(1, 0, NULL), route, mxs::Reply());
    }

    if (send_error)
    {
        GWBUF* err_pkt = mysql_create_custom_error(1, 0, 2003, "Invalid insert target");
        mxs::ReplyRoute route;
        rc = FilterSession::clientReply(err_pkt, route, mxs::Reply());
    }
    else
    {
        rc = FilterSession::routeQuery(queue);
    }

    return rc;
}

int32_t InsertStreamSession::clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    int rc = 1;

    if (m_state == DS_CLOSING_STREAM || (m_state == DS_REQUEST_SENT && !reply.error()))
    {
        gwbuf_free(buffer);
        mxb_assert(!m_queue.empty());

        if (m_state == DS_CLOSING_STREAM
            && qc_query_is_type(qc_get_type_mask(m_queue.get()), QUERY_TYPE_COMMIT))
        {
            // TODO: This must be done as the LOAD DATA LOCAL INFILE disables the client-side tracking of the
            // transaction state. The LOAD DATA LOCAL INFILE tracking would have to be done independently by
            // all components in the routing chain to make it work correctly.
            auto mariases = static_cast<MYSQL_session*>(m_pSession->protocol_data());
            mariases->trx_state = MYSQL_session::TRX_INACTIVE;
        }

        m_state = m_state == DS_CLOSING_STREAM ? DS_STREAM_CLOSED : DS_REQUEST_ACCEPTED;

        GWBUF* queue = m_queue.release();

        if (m_state == DS_REQUEST_ACCEPTED)
        {
            /** The request is packet 0 and the response is packet 1 so we'll
             * have to send the data in packet number 2 */
            m_packet_num++;
        }

        session_delay_routing(m_pSession, this, queue, 0);
    }
    else
    {
        rc = FilterSession::clientReply(buffer, down, reply);
    }

    return rc;
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::EXPERIMENTAL,
        MXS_FILTER_VERSION,
        "Data streaming filter",
        "1.0.0",
        CAPS,
        &mxs::FilterApi<InsertStream>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {"source",          MXS_MODULE_PARAM_STRING },
            {"user",            MXS_MODULE_PARAM_STRING },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
