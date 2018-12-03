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

#include <maxscale/modutil.hh>
#include <maxscale/protocol/mysql.h>
#include <maxscale/log.h>

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
    m_expected_rows -= modutil_count_packets(buffer);
    mxb_assert(m_expected_rows >= 0);
    return m_expected_rows == 0;
}

static inline bool have_next_packet(GWBUF* buffer)
{
    uint32_t len = MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buffer)) + MYSQL_HEADER_LEN;
    return gwbuf_length(buffer) > len;
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
        bool more = false;
        int n_eof = modutil_count_signal_packets(buffer, 0, &more, &m_modutil_state);

        // If the server responded with an error, n_eof > 0
        if (n_eof > 0 || consume_fetched_rows(buffer))
        {
            set_reply_state(REPLY_STATE_DONE);
        }
    }
    else if (current_command() == MXS_COM_STATISTICS)
    {
        // COM_STATISTICS returns a single string and thus requires special handling
        set_reply_state(REPLY_STATE_DONE);
    }
    else if (get_reply_state() == REPLY_STATE_START
             && (!mxs_mysql_is_result_set(buffer) || GWBUF_IS_COLLECTED_RESULT(buffer)))
    {
        m_local_infile_requested = false;

        if (GWBUF_IS_COLLECTED_RESULT(buffer)
            || current_command() == MXS_COM_STMT_PREPARE
            || !mxs_mysql_is_ok_packet(buffer)
            || !mxs_mysql_more_results_after_ok(buffer))
        {
            /** Not a result set, we have the complete response */
            set_reply_state(REPLY_STATE_DONE);

            if (mxs_mysql_is_local_infile(buffer))
            {
                m_local_infile_requested = true;
            }
        }
        else
        {
            // This is an OK packet and more results will follow
            mxb_assert(mxs_mysql_is_ok_packet(buffer)
                       && mxs_mysql_more_results_after_ok(buffer));

            if (have_next_packet(buffer))
            {
                // TODO: Don't clone the buffer
                GWBUF* tmp = gwbuf_clone(buffer);
                tmp = gwbuf_consume(tmp, mxs_mysql_get_packet_len(tmp));
                process_reply(tmp);
                gwbuf_free(tmp);
                return;
            }
        }
    }
    else
    {
        bool more = false;
        int n_old_eof = get_reply_state() == REPLY_STATE_RSET_ROWS ? 1 : 0;
        int n_eof = modutil_count_signal_packets(buffer, n_old_eof, &more, &m_modutil_state);

        if (n_eof > 2)
        {
            /**
             * We have multiple results in the buffer, we only care about
             * the state of the last one. Skip the complete result sets and act
             * like we're processing a single result set.
             */
            n_eof = n_eof % 2 ? 1 : 2;
        }

        if (n_eof == 0)
        {
            /** Waiting for the EOF packet after the column definitions */
            set_reply_state(REPLY_STATE_RSET_COLDEF);
        }
        else if (n_eof == 1 && current_command() != MXS_COM_FIELD_LIST)
        {
            /** Waiting for the EOF packet after the rows */
            set_reply_state(REPLY_STATE_RSET_ROWS);

            if (is_opening_cursor())
            {
                set_cursor_opened();
                MXS_INFO("Cursor successfully opened");
                set_reply_state(REPLY_STATE_DONE);
            }
        }
        else
        {
            /** We either have a complete result set or a response to
             * a COM_FIELD_LIST command */
            mxb_assert(n_eof == 2 || (n_eof == 1 && current_command() == MXS_COM_FIELD_LIST));
            set_reply_state(REPLY_STATE_DONE);

            if (more)
            {
                /** The server will send more resultsets */
                set_reply_state(REPLY_STATE_START);
            }
        }
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
