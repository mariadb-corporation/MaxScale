#include "rwbackend.hh"

#include <maxscale/modutil.h>
#include <maxscale/log_manager.h>

namespace maxscale
{

RWBackend::RWBackend(SERVER_REF* ref):
    mxs::Backend(ref),
    m_reply_state(REPLY_STATE_DONE),
    m_modutil_state({}),
    m_command(0),
    m_local_infile_requested(false)
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
        }
    }

    return mxs::Backend::write(buffer);
}

void RWBackend::close(close_type type)
{
    m_reply_state = REPLY_STATE_DONE;
    mxs::Backend::close(type);
}

static inline bool have_next_packet(GWBUF* buffer)
{
    uint32_t len = MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buffer)) + MYSQL_HEADER_LEN;
    return gwbuf_length(buffer) > len;
}

/**
 * @brief Check if we have received a complete reply from the backend
 *
 * @param backend Backend reference
 * @param buffer  Buffer containing the response
 *
 * @return True if the complete response has been received
 */
bool RWBackend::reply_is_complete(GWBUF *buffer)
{
    if (GWBUF_IS_COLLECTED_RESULT(buffer))
    {
        set_reply_state(REPLY_STATE_DONE);
        return true;
    }

    while (buffer)
    {
        if (GWBUF_IS_REPLY_LAST(buffer))
        {
            set_reply_state(REPLY_STATE_DONE);
            return true;
        }
        else if (GWBUF_IS_REPLY_LINFILE(buffer))
        {
            m_local_infile_requested = true;
            set_reply_state(REPLY_STATE_DONE);
            return true;
        }
        buffer = buffer->next;
    }

    return false;
}

SRWBackendList RWBackend::from_servers(SERVER_REF* servers)
{
    SRWBackendList backends;

    for (SERVER_REF *ref = servers; ref; ref = ref->next)
    {
        if (ref->active)
        {
            backends.push_back(mxs::SRWBackend(new mxs::RWBackend(ref)));
        }
    }

    return backends;
}

}
