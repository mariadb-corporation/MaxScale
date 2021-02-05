/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <deque>
#include <list>
#include <string>
#include <memory>

#include <maxscale/service.hh>
#include <maxbase/stopwatch.hh>


namespace maxscale
{

class Backend
{
    Backend(const Backend&);
    Backend& operator=(const Backend&);
public:

    /**
     * How is the backend being closed
     */
    enum close_type
    {
        CLOSE_NORMAL,
        CLOSE_FATAL
    };

    /**
     * What type of a response we expect from the backend
     */
    enum response_type
    {
        EXPECT_RESPONSE,    // Response will be routed to the client
        IGNORE_RESPONSE,    // Response will be discarded by the router
        NO_RESPONSE         // No response will be generated
    };

    /**
     * @brief Create new Backend
     *
     * @param endpoint The downstream endpoint to connect to
     */
    Backend(mxs::Endpoint* endpoint);

    virtual ~Backend();

    /**
     * @brief Get pointer to server reference
     *
     * @return Pointer to server reference
     */
    inline mxs::Endpoint* backend() const
    {
        mxb_assert(m_backend);
        return m_backend;
    }

    /**
     * @brief Get pointer to server reference
     *
     * @return Pointer to server reference
     */
    inline mxs::Target* target() const
    {
        mxb_assert(m_backend);
        return m_backend->target();
    }

    /**
     * @brief Check if a connection to this backend can be made
     *
     * @return True if the backend has not failed and a connection can be attempted
     */
    inline bool can_connect() const
    {
        return !has_failed() && m_backend->target()->is_connectable();
    }

    /**
     * @brief Create a new connection
     *
     * @return True if connection was successfully created
     */
    bool connect();

    /**
     * @brief Close the backend
     *
     * This will close all active connections created by the backend.
     */
    virtual void close(close_type type = CLOSE_NORMAL);

    /**
     * @brief Write data to the backend server
     *
     * @param buffer          Buffer containing the data to write
     * @param expect_response Whether to expect a response to the query
     *
     * @return True if data was written successfully
     */
    virtual bool write(GWBUF* buffer, response_type type = EXPECT_RESPONSE);

    /**
     * @brief Mark that a reply to a query was received and processed
     */
    void ack_write();

    /**
     * @brief Check if backend is in use
     *
     * @return True if backend is in use
     */
    inline bool in_use() const
    {
        return m_state & IN_USE;
    }

    /**
     * @brief Check if the backend server reference is active
     *
     * @return True if the server reference is active
     */
    inline bool is_active() const
    {
        return m_backend->target()->active();
    }

    /**
     * @brief Check if backend is waiting for a result
     *
     * @return True if backend is waiting for a result
     */
    inline bool is_waiting_result() const
    {
        return std::find(m_responses.begin(), m_responses.end(), EXPECT_RESPONSE) != m_responses.end();
    }

    /**
     * @brief Check if the next response from this backend should be ignored
     *
     * @return True if the result should be ignored
     */
    bool should_ignore_response() const
    {
        return !m_responses.empty() && m_responses.front() == IGNORE_RESPONSE;
    }

    /**
     * @brief Check if the backend is closed
     *
     * @return True if the backend is closed
     */
    inline bool is_closed() const
    {
        return m_closed;
    }

    /**
     * @brief Check if the server is a master
     *
     * @return True if server is a master
     */
    inline bool is_master() const
    {
        return m_backend->target()->is_master();
    }

    /**
     * @brief Check if the server is a slave
     *
     * @return True if the server is a slave
     */
    inline bool is_slave() const
    {
        return m_backend->target()->is_slave();
    }

    /**
     * @brief Check if the server is a relay server
     *
     * @return True if the server is a relay server
     */
    inline bool is_relay() const
    {
        return m_backend->target()->is_relay();
    }

    /**
     * @brief Check if the backend has failed fatally
     *
     * When a fatal failure occurs in a backend, the backend server can no longer
     * be used by this session. Fatal failures can occur when the execution of
     * a session command fails on the backend but the expected result is different.
     *
     * @return True if a fatal failure has occurred in the backend server
     */
    inline bool has_failed() const
    {
        return m_state & FATAL_FAILURE;
    }

    /**
     * @brief Get the object name of this server
     *
     * @return The unique object name of this server
     */
    inline const char* name() const
    {
        return m_backend->target()->name();
    }

    virtual void select_started();
    virtual void select_finished();

    int64_t                       num_selects() const;
    const maxbase::StopWatch&     session_timer() const;
    const maxbase::IntervalTimer& select_timer() const;

    /**
     * Get verbose status description
     *
     * @return A verbose description of the backend's status
     */
    std::string get_verbose_status() const;

    /**
     * Add explanation message to latest close reason
     *
     * The message is printed in get_verbose_status() if the backend is closed.
     *
     * @param reason The human-readable message
     */
    void set_close_reason(const std::string& reason);

    /**
     * Get latest close reason
     *
     * @return A human-readable reason why the connection was closed
     */
    const std::string& close_reason() const
    {
        return m_close_reason;
    }

private:
    /**
     * Internal state of the backend
     */
    enum backend_state
    {
        IN_USE        = 0x01,   /**< Backend has been taken into use */
        FATAL_FAILURE = 0x02    /**< Backend references that should be dropped */
    };

    /**
     * @brief Clear state
     *
     * @param state State to clear
     */
    void clear_state(backend_state state);

    /**
     * @brief Set state
     *
     * @param state State to set
     */
    void set_state(backend_state state);

    // Stringification function
    static std::string to_string(backend_state state);

    bool           m_closed {false};        /**< True if a connection has been opened and closed */
    time_t         m_closed_at {0};         /**< Timestamp when the backend was last closed */
    std::string    m_close_reason;          /**< Why the backend was closed */
    time_t         m_opened_at {0};         /**< Timestamp when the backend was last opened */
    mxs::Endpoint* m_backend {nullptr};     /**< Backend server */
    mxs::Buffer    m_pending_cmd;           /**< Pending commands */
    int            m_state {0};             /**< State of the backend */

    maxbase::StopWatch     m_session_timer;
    maxbase::IntervalTimer m_select_timer;
    int64_t                m_num_selects {0};

    // Contains the types of responses we're expecting from this backend. Used to detect if multiple commands
    // were sent to the backend but not all of the results should be sent to the client.
    std::deque<response_type> m_responses;
};

typedef std::shared_ptr<Backend> SBackend;
typedef std::list<SBackend>      BackendList;
}
