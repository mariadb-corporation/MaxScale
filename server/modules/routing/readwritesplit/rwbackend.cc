#include "rwbackend.hh"

#include <maxscale/modutil.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/log_manager.h>

namespace maxscale
{

RWBackend::RWBackend(SERVER_REF* ref):
    mxs::Backend(ref),
    m_reply_state(REPLY_STATE_DONE),
    m_large_packet(false)
{
}

RWBackend::~RWBackend()
{
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
    if (get_reply_state() == REPLY_STATE_START &&
        (!mxs_mysql_is_result_set(buffer) || GWBUF_IS_COLLECTED_RESULT(buffer)))
    {
        if (GWBUF_IS_COLLECTED_RESULT(buffer) ||
            current_command() == MXS_COM_STMT_PREPARE ||
            !mxs_mysql_is_ok_packet(buffer) ||
            !mxs_mysql_more_results_after_ok(buffer))
        {
            /** Not a result set, we have the complete response */
            set_reply_state(REPLY_STATE_DONE);
        }
        else
        {
            // This is an OK packet and more results will follow
            ss_dassert(mxs_mysql_is_ok_packet(buffer) &&
                mxs_mysql_more_results_after_ok(buffer));

            if (have_next_packet(buffer))
            {
                set_reply_state(REPLY_STATE_RSET_COLDEF);
                return reply_is_complete(buffer);
            }
        }
    }
    else
    {
        bool more = false;
        modutil_state state = {is_large_packet()};
        int n_old_eof = get_reply_state() == REPLY_STATE_RSET_ROWS ? 1 : 0;
        int n_eof = modutil_count_signal_packets(buffer, n_old_eof, &more, &state);
        set_large_packet(state.state);

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
        }
        else
        {
            /** We either have a complete result set or a response to
             * a COM_FIELD_LIST command */
            ss_dassert(n_eof == 2 || (n_eof == 1 && current_command() == MXS_COM_FIELD_LIST));
            set_reply_state(REPLY_STATE_DONE);

            if (more)
            {
                /** The server will send more resultsets */
                set_reply_state(REPLY_STATE_START);
            }
        }
    }

    return get_reply_state() == REPLY_STATE_DONE;
}

}
