/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <map>
#include <memory>
#include <algorithm>

#include <maxscale/backend.hh>
#include <maxscale/modutil.hh>
#include <maxscale/response_stat.hh>
#include <maxscale/protocol/mysql.hh>

namespace maxscale
{

/** Move this somewhere else */
template<typename Smart>
std::vector<typename Smart::pointer> sptr_vec_to_ptr_vec(const std::vector<Smart>& sVec)
{
    std::vector<typename Smart::pointer> pVec;
    std::for_each(sVec.begin(), sVec.end(), [&pVec](const Smart& smart) {
                      pVec.push_back(smart.get());
                  });
    return pVec;
}

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

// All interfacing is now handled via RWBackend*.
using PRWBackends = std::vector<RWBackend*>;

// Internal storage for a class containing RWBackend:s.
using SRWBackends = std::vector<std::unique_ptr<RWBackend>>;

class RWBackend : public mxs::Backend
{
    RWBackend(const RWBackend&);
    RWBackend& operator=(const RWBackend&);

public:
    class Error
    {
    public:
        Error() = default;

        explicit operator bool() const
        {
            return m_code != 0;
        }

        bool is_rollback() const
        {
            bool rv = false;

            if (m_code != 0)
            {
                mxb_assert(m_sql_state.length() == 5);
                // The 'sql_state' of all transaction rollbacks is "40XXX".
                if (m_sql_state[0] == '4' && m_sql_state[1] == '0')
                {
                    rv = true;
                }
            }

            return rv;
        }

        bool is_wsrep_error() const
        {
            return m_code == 1047
                   && m_sql_state == "08S01"
                   && m_message == "WSREP has not yet prepared node for application use";
        }

        bool is_unexpected_error() const
        {
            switch (m_code)
            {
            case ER_CONNECTION_KILLED:
            case ER_SERVER_SHUTDOWN:
            case ER_NORMAL_SHUTDOWN:
            case ER_SHUTDOWN_COMPLETE:
                return true;

            default:
                return false;
            }
        }

        uint32_t code() const
        {
            return m_code;
        }

        const std::string& sql_state() const
        {
            return m_sql_state;
        }

        const std::string& message() const
        {
            return m_message;
        }

        template<class InputIterator>
        void set(uint32_t code,
                 InputIterator sql_state_begin, InputIterator sql_state_end,
                 InputIterator message_begin, InputIterator message_end)
        {
            mxb_assert(std::distance(sql_state_begin, sql_state_end) == 5);
            m_code = code;
            m_sql_state.assign(sql_state_begin, sql_state_end);
            m_message.assign(message_begin, message_end);
        }

        void clear()
        {
            m_code = 0;
            m_sql_state.clear();
            m_message.clear();
        }

    private:
        uint16_t    m_code {0};
        std::string m_sql_state;
        std::string m_message;
    };

    static SRWBackends from_servers(SERVER_REF* servers);

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
     * Updated during the call to @c process_reply().
     *
     * @return The current error state.
     */
    const Error& error() const
    {
        return m_error;
    }

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
        return m_size > 0 && m_reply_state != REPLY_STATE_DONE;
    }

    void process_packets(GWBUF* buffer);

    // Controlled by the session
    ResponseStat& response_stat();

    /**
     * Change server replication lag state and log warning when state changes.
     *
     * @param new_state New replication lag state
     * @param max_rlag Maximum allowed lag. Used for the log message.
     */
    void change_rlag_state(SERVER::RLagState new_state, int max_rlag);

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
    bool             m_large_query = false;
    bool             m_skip_next = false;
    Error            m_error;
    uint64_t         m_size = 0;/**< Size of the response */

    /**
     * @param it   Iterator pointing to the command byte of an error packet.
     * @param end  Iterator pointing one past the end of the error packet.
     */
    void process_reply_start(mxs::Buffer::iterator it, mxs::Buffer::iterator end);

    /**
     * Update @c m_error.
     *
     * @param it   Iterator that points to the first byte of the error code in an error packet.
     * @param end  Iterator pointing one past the end of the error packet.
     */
    void update_error(mxs::Buffer::iterator it, mxs::Buffer::iterator end);

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
