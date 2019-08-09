/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <map>
#include <memory>

#include <maxscale/backend.hh>
#include <maxscale/modutil.h>
#include <maxscale/response_stat.hh>

namespace maxscale
{

/** Enum for tracking client reply state */
enum reply_state_t
{
    REPLY_STATE_START,          /**< Query sent to backend */
    REPLY_STATE_DONE,           /**< Complete reply received */
    REPLY_STATE_RSET_COLDEF,    /**< Resultset response, waiting for column definitions */
    REPLY_STATE_RSET_COLDEF_EOF,/**< Resultset response, waiting for EOF for column definitions */
    REPLY_STATE_RSET_ROWS       /**< Resultset response, waiting for rows */
};

typedef std::map<uint32_t, uint32_t> BackendHandleMap;      /** Internal ID to external ID */

class RWBackend;
typedef std::shared_ptr<RWBackend> SRWBackend;
typedef std::list<SRWBackend>      SRWBackendList;

class RWBackend : public mxs::Backend
{
    RWBackend(const RWBackend&);
    RWBackend& operator=(const RWBackend&);

public:

    static SRWBackendList from_servers(SERVER_REF* servers);

    RWBackend(SERVER_REF* ref);
    virtual ~RWBackend();

    inline reply_state_t get_reply_state() const
    {
        return m_reply_state;
    }

    const char* reply_state_str() const
    {
        switch (m_reply_state)
        {
        case REPLY_STATE_START:
            return "START";

        case REPLY_STATE_DONE:
            return "DONE";

        case REPLY_STATE_RSET_COLDEF:
            return "COLDEF";

        case REPLY_STATE_RSET_COLDEF_EOF:
            return "COLDEF_EOF";

        case REPLY_STATE_RSET_ROWS:
            return "ROWS";

        default:
            return "UNKNOWN";
        }
    }

    void     add_ps_handle(uint32_t id, uint32_t handle);
    uint32_t get_ps_handle(uint32_t id) const;

    bool execute_session_command();
    bool continue_session_command(GWBUF* buffer);

    /**
     * Write a query to the backend
     *
     * This function handles the replacement of the prepared statement IDs from
     * the internal ID to the server specific one. Trailing parts of large
     * packets should use RWBackend::continue_write.
     *
     * @param buffer Buffer to write
     * @param type   Whether a response is expected
     *
     * @return True if writing was successful
     */
    bool write(GWBUF* buffer, response_type type = EXPECT_RESPONSE);

    /**
     * Continue a previously started write
     *
     * This should only be used when RWBackend::write has been called to start
     * a new query.
     *
     * @param buffer Buffer to write
     *
     * @return True if writing was successful
     */
    bool continue_write(GWBUF* buffer)
    {
        return mxs::Backend::write(buffer, Backend::NO_RESPONSE);
    }

    void close(close_type type = CLOSE_NORMAL);

    // For COM_STMT_FETCH processing
    bool consume_fetched_rows(GWBUF* buffer);

    inline uint8_t current_command() const
    {
        return m_command;
    }

    bool local_infile_requested() const
    {
        return m_local_infile_requested;
    }

    void process_reply(GWBUF* buffer);

    /**
     * Check whether the response from the server is complete
     *
     * @return True if no more results are expected from this server
     */
    bool reply_is_complete() const
    {
        return m_reply_state == REPLY_STATE_DONE;
    }

    /**
     * Check if a partial response has been received from the backend
     *
     * @return True if some parts of the reply have been received
     */
    bool reply_has_started() const
    {
        return m_reply_state != REPLY_STATE_START && m_reply_state != REPLY_STATE_DONE;
    }

    void process_packets(GWBUF* buffer);
    void process_reply_start(mxs::Buffer::iterator it);

    // Controlled by the session
    ResponseStat& response_stat();

    uint64_t get_lru_tick() const
    {
        return m_lru_tick;
    }

private:
    reply_state_t    m_reply_state;
    BackendHandleMap m_ps_handles;      /**< Internal ID to backend PS handle mapping */
    modutil_state    m_modutil_state;   /**< @see modutil_count_signal_packets */
    uint8_t          m_command;
    bool             m_opening_cursor;          /**< Whether we are opening a cursor */
    uint32_t         m_expected_rows;           /**< Number of rows a COM_STMT_FETCH is retrieving */
    bool             m_local_infile_requested;  /**< Whether a LOCAL INFILE was requested */
    ResponseStat     m_response_stat;
    uint64_t         m_num_coldefs = 0;
    bool             m_skip_next = false;
    uint64_t         m_lru_tick = 0;

    inline bool is_opening_cursor() const
    {
        return m_opening_cursor;
    }

    inline void set_cursor_opened()
    {
        m_opening_cursor = false;
    }

    inline void set_reply_state(reply_state_t state)
    {
        m_reply_state = state;
    }
};
}
