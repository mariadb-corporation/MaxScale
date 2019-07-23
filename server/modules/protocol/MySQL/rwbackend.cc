/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/rwbackend.hh>

#include <maxscale/modutil.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/log.h>

using Iter = mxs::Buffer::iterator;

namespace maxscale
{

RWBackend::RWBackend(SERVER_REF* ref)
    : mxs::Backend(ref)
    , m_reply_state(REPLY_STATE_DONE)
    , m_modutil_state{0}
    , m_command(0)
    , m_opening_cursor(false)
    , m_expected_rows(0)
    , m_local_infile_requested(false)
{
}

RWBackend::~RWBackend()
{
}

bool RWBackend::execute_session_command()
{
    m_command = next_session_command()->get_command();
    bool expect_response = mxs_mysql_command_will_respond(m_command);
    bool rval = mxs::Backend::execute_session_command();

    if (rval && expect_response)
    {
        set_reply_state(REPLY_STATE_START);
    }

    return rval;
}

bool RWBackend::continue_session_command(GWBUF* buffer)
{
    return Backend::write(buffer, NO_RESPONSE);
}

void RWBackend::add_ps_handle(uint32_t id, uint32_t handle)
{
    m_ps_handles[id] = handle;
    MXS_INFO("PS response for %s: %u -> %u", name(), id, handle);
}

uint32_t RWBackend::get_ps_handle(uint32_t id) const
{
    BackendHandleMap::const_iterator it = m_ps_handles.find(id);

    if (it != m_ps_handles.end())
    {
        return it->second;
    }

    return 0;
}

bool RWBackend::write(GWBUF* buffer, response_type type)
{
    if (type == mxs::Backend::EXPECT_RESPONSE)
    {
        /** The server will reply to this command */
        set_reply_state(REPLY_STATE_START);
    }

    uint8_t cmd = mxs_mysql_get_command(buffer);

    m_command = cmd;

    if (mxs_mysql_is_ps_command(cmd))
    {
        // We need to completely separate the buffer this backend owns and the one that the caller owns to
        // prevent any modifications from affecting the one that was written through this backend. If the
        // buffer gets placed into the write queue of the DCB, subsequent modifications to the original buffer
        // would be propagated to the one this backend owns.
        GWBUF* tmp = gwbuf_deep_clone(buffer);
        gwbuf_free(buffer);
        buffer = tmp;

        uint32_t id = mxs_mysql_extract_ps_id(buffer);
        BackendHandleMap::iterator it = m_ps_handles.find(id);

        if (it != m_ps_handles.end())
        {
            /** Replace the client handle with the real PS handle */
            uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
            gw_mysql_set_byte4(ptr, it->second);

            if (cmd == MXS_COM_STMT_EXECUTE)
            {
                // Extract the flag byte after the statement ID
                uint8_t flags = 0;
                gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET + MYSQL_PS_ID_SIZE, 1, &flags);

                // Any non-zero flag value means that we have an open cursor
                m_opening_cursor = flags != 0;
            }
            else if (cmd == MXS_COM_STMT_CLOSE)
            {
                m_ps_handles.erase(it);
            }
            else if (cmd == MXS_COM_STMT_FETCH)
            {
                // Number of rows to fetch is a 4 byte integer after the ID
                uint8_t buf[4];
                gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET + MYSQL_PS_ID_SIZE, 4, buf);
                m_expected_rows = gw_mysql_get_byte4(buf);
            }
        }
    }

    return mxs::Backend::write(buffer, type);
}

void RWBackend::close(close_type type)
{
    m_reply_state = REPLY_STATE_DONE;
    mxs::Backend::close(type);
}

bool RWBackend::consume_fetched_rows(GWBUF* buffer)
{
    bool rval = false;
    bool more = false;
    int n_eof = modutil_count_signal_packets(buffer, 0, &more, &m_modutil_state);

    // If the server responded with an error, n_eof > 0
    if (n_eof > 0)
    {
        rval = true;
    }
    else
    {
        m_expected_rows -= modutil_count_packets(buffer);
        mxb_assert(m_expected_rows >= 0);
        rval = m_expected_rows == 0;
    }

    return rval;
}

static inline bool have_next_packet(GWBUF* buffer)
{
    uint32_t len = MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buffer)) + MYSQL_HEADER_LEN;
    return gwbuf_length(buffer) > len;
}

uint64_t get_encoded_int(Iter it)
{
    uint64_t len = *it++;

    switch (len)
    {
    case 0xfc:
        len = *it++;
        len |= ((uint64_t)*it++) << 8;
        break;

    case 0xfd:
        len = *it++;
        len |= ((uint64_t)*it++) << 8;
        len |= ((uint64_t)*it++) << 16;
        break;

    case 0xfe:
        len = *it++;
        len |= ((uint64_t)*it++) << 8;
        len |= ((uint64_t)*it++) << 16;
        len |= ((uint64_t)*it++) << 24;
        len |= ((uint64_t)*it++) << 32;
        len |= ((uint64_t)*it++) << 40;
        len |= ((uint64_t)*it++) << 48;
        len |= ((uint64_t)*it++) << 56;
        break;

    default:
        break;
    }

    return len;
}

Iter skip_encoded_int(Iter it)
{
    switch (*it)
    {
    case 0xfc:
        it.advance(3);
        break;

    case 0xfd:
        it.advance(4);
        break;

    case 0xfe:
        it.advance(9);
        break;

    default:
        ++it;
        break;
    }

    return it;
}

bool is_last_ok(Iter it)
{
    ++it;                       // Skip the command byte
    it = skip_encoded_int(it);  // Affected rows
    it = skip_encoded_int(it);  // Last insert ID
    uint16_t status = *it++;
    status |= (*it++) << 8;
    return (status & SERVER_MORE_RESULTS_EXIST) == 0;
}

bool is_last_eof(Iter it)
{
    std::advance(it, 3);    // Skip the command byte and warning count
    uint16_t status = *it++;
    status |= (*it++) << 8;
    return (status & SERVER_MORE_RESULTS_EXIST) == 0;
}

void RWBackend::process_reply_start(Iter it)
{
    uint8_t cmd = *it;
    m_local_infile_requested = false;

    switch (cmd)
    {
    case MYSQL_REPLY_OK:
        if (is_last_ok(it))
        {
            // No more results
            set_reply_state(REPLY_STATE_DONE);
        }
        break;

    case MYSQL_REPLY_LOCAL_INFILE:
        m_local_infile_requested = true;
        set_reply_state(REPLY_STATE_DONE);
        break;

    case MYSQL_REPLY_ERR:
        // Nothing ever follows an error packet
        set_reply_state(REPLY_STATE_DONE);
        break;

    case MYSQL_REPLY_EOF:
        // EOF packets are never expected as the first response
        mxb_assert(!true);
        break;

    default:
        if (current_command() == MXS_COM_FIELD_LIST)
        {
            // COM_FIELD_LIST sends a strange kind of a result set
            set_reply_state(REPLY_STATE_RSET_ROWS);
        }
        else
        {
            // Start of a result set
            m_num_coldefs = get_encoded_int(it);
            set_reply_state(REPLY_STATE_RSET_COLDEF);
        }

        break;
    }
}

void RWBackend::process_packets(GWBUF* result)
{
    mxs::Buffer buffer(result);
    auto it = buffer.begin();
    MXB_AT_DEBUG(size_t total_len = buffer.length());
    MXB_AT_DEBUG(size_t used_len = 0);
    mxb_assert(dcb()->session->service->capabilities & (RCAP_TYPE_PACKET_OUTPUT | RCAP_TYPE_STMT_OUTPUT));

    while (it != buffer.end())
    {
        // Extract packet length and command byte
        uint32_t len = *it++;
        len |= (*it++) << 8;
        len |= (*it++) << 16;
        ++it;   // Skip the sequence
        mxb_assert(it != buffer.end());
        mxb_assert(used_len + len <= total_len);
        MXB_AT_DEBUG(used_len += len);
        auto end = it;
        end.advance(len);
        uint8_t cmd = *it;

        // Ignore the tail end of a large packet large packet. Only resultsets can generate packets this large
        // and we don't care what the contents are and thus it is safe to ignore it.
        bool skip_next = m_skip_next;
        m_skip_next = len == GW_MYSQL_MAX_PACKET_LEN;

        if (skip_next)
        {
            it = end;
            continue;
        }

        switch (m_reply_state)
        {
        case REPLY_STATE_START:
            process_reply_start(it);
            break;

        case REPLY_STATE_DONE:
            if (cmd == MYSQL_REPLY_ERR)
            {
                // Unexpected error at the end of a resultset, possibly a killed connection
            }
            else
            {
                // This should never happen
                MXS_ERROR("Unexpected result state. cmd: 0x%02hhx, len: %u", cmd, len);
                mxb_assert(!true);
            }
            break;

        case REPLY_STATE_RSET_COLDEF:
            mxb_assert(m_num_coldefs > 0);
            --m_num_coldefs;

            if (m_num_coldefs == 0)
            {
                set_reply_state(REPLY_STATE_RSET_COLDEF_EOF);
                // Skip this state when DEPRECATE_EOF capability is supported
            }
            break;

        case REPLY_STATE_RSET_COLDEF_EOF:
            mxb_assert(cmd == MYSQL_REPLY_EOF && len == MYSQL_EOF_PACKET_LEN - MYSQL_HEADER_LEN);
            set_reply_state(REPLY_STATE_RSET_ROWS);

            if (is_opening_cursor())
            {
                set_cursor_opened();
                MXS_INFO("Cursor successfully opened");
                set_reply_state(REPLY_STATE_DONE);
            }
            break;

        case REPLY_STATE_RSET_ROWS:
            if (cmd == MYSQL_REPLY_EOF && len == MYSQL_EOF_PACKET_LEN - MYSQL_HEADER_LEN)
            {
                set_reply_state(is_last_eof(it) ? REPLY_STATE_DONE : REPLY_STATE_START);
            }
            else if (cmd == MYSQL_REPLY_ERR)
            {
                set_reply_state(REPLY_STATE_DONE);
            }
            break;
        }

        it = end;
    }

    buffer.release();
}

/**
 * @brief Process a possibly partial response from the backend
 *
 * @param buffer  Buffer containing the response
 */
void RWBackend::process_reply(GWBUF* buffer)
{
    if (current_command() == MXS_COM_STMT_FETCH)
    {
        // If the server responded with an error, n_eof > 0
        if (consume_fetched_rows(buffer))
        {
            set_reply_state(REPLY_STATE_DONE);
        }
    }
    else if (current_command() == MXS_COM_STATISTICS || GWBUF_IS_COLLECTED_RESULT(buffer))
    {
        // COM_STATISTICS returns a single string and thus requires special handling.
        // Collected result are all in one buffer and need no processing.
        set_reply_state(REPLY_STATE_DONE);
    }
    else
    {
        // Normal result, process it one packet at a time
        process_packets(buffer);
    }

    if (get_reply_state() == REPLY_STATE_DONE)
    {
        ack_write();
    }
}

ResponseStat& RWBackend::response_stat()
{
    return m_response_stat;
}

SRWBackendList RWBackend::from_servers(SERVER_REF* servers)
{
    SRWBackendList backends;

    for (SERVER_REF* ref = servers; ref; ref = ref->next)
    {
        if (ref->active)
        {
            backends.push_back(mxs::SRWBackend(new mxs::RWBackend(ref)));
        }
    }

    return backends;
}
}
