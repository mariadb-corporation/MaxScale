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
    m_skip(false)
{
}

RWBackend::~RWBackend()
{
}

reply_state_t RWBackend::get_reply_state() const
{
    return m_reply_state;
}

void RWBackend::set_reply_state(reply_state_t state)
{
    m_reply_state = state;
}

void RWBackend::set_skip_packet(bool state)
{
    m_skip = state;
}

bool RWBackend::get_skip_packet() const
{
    return m_skip;
}

bool RWBackend::execute_session_command()
{
    bool expect_response = mxs_mysql_command_will_respond(next_session_command()->get_command());
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

    if (is_ps_command(cmd))
    {
        uint32_t id = mxs_mysql_extract_ps_id(buffer);
        BackendHandleMap::iterator it = m_ps_handles.find(id);

        if (it != m_ps_handles.end())
        {
            /** Replace the client handle with the real PS handle */
            uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
            gw_mysql_set_byte4(ptr, it->second);
        }
    }

    return mxs::Backend::write(buffer);
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
