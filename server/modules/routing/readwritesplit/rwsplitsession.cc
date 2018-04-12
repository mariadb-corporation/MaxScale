/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rwsplitsession.hh"
#include "rwsplit_internal.hh"

RWBackend::RWBackend(SERVER_REF* ref):
    mxs::Backend(ref),
    m_reply_state(REPLY_STATE_DONE),
    m_large_packet(false),
    m_command(0),
    m_open_cursor(false),
    m_expected_rows(0)
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
    uint8_t cmd = mxs_mysql_get_command(buffer);

    m_command = cmd;

    if (is_ps_command(cmd))
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
                m_open_cursor = flags != 0;
            }
            else if (cmd == MXS_COM_STMT_FETCH)
            {
                ss_dassert(m_open_cursor);
                // Number of rows to fetch is a 4 byte integer after the ID
                uint8_t buf[4];
                gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET + MYSQL_PS_ID_SIZE, 4, buf);
                m_expected_rows = gw_mysql_get_byte4(buf);
            }
            else
            {
                m_open_cursor = false;
            }
        }
    }

    return mxs::Backend::write(buffer);
}

bool RWBackend::consume_fetched_rows(GWBUF* buffer)
{
    m_expected_rows -= modutil_count_packets(buffer);
    ss_dassert(m_expected_rows >= 0);
    return m_expected_rows == 0;
}

uint32_t get_internal_ps_id(RWSplitSession* rses, GWBUF* buffer)
{
    uint32_t rval = 0;

    // All COM_STMT type statements store the ID in the same place
    uint32_t id = mxs_mysql_extract_ps_id(buffer);
    ClientHandleMap::iterator it = rses->ps_handles.find(id);

    if (it != rses->ps_handles.end())
    {
        rval = it->second;
    }
    else
    {
        MXS_WARNING("Client requests unknown prepared statement ID '%u' that "
                    "does not map to an internal ID", id);
    }

    return rval;
}

RouteInfo::RouteInfo(RWSplitSession* rses, GWBUF* buffer):
    target(TARGET_UNDEFINED),
    command(0xff),
    type(QUERY_TYPE_UNKNOWN),
    stmt_id(0)
{
    target = get_target_type(rses, buffer, &command, &type, &stmt_id);
}
