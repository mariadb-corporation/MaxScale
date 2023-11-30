/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <algorithm>

#include <maxscale/target.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/small_vector.hh>


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
    enum response_type : uint8_t
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
        mxb_assert(m_target);
        return m_target;
    }

    /**
     * @brief Check if a connection to this backend can be made
     *
     * @return True if the backend has not failed and a connection can be attempted
     */
    inline bool can_connect() const
    {
        return !has_failed() && m_target->is_connectable();
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
    virtual bool write(GWBUF&& buffer, response_type type = EXPECT_RESPONSE);

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
        return m_state == IN_USE;
    }

    /**
     * @brief Check if the backend server reference is active
     *
     * @return True if the server reference is active
     */
    inline bool is_active() const
    {
        return m_target->active();
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
     * @brief Check whether the next response from this backend should be routed to the client
     *
     * @return True if this response should be routed to the client
     */
    bool is_expected_response() const
    {
        return !m_responses.empty() && m_responses.front() == EXPECT_RESPONSE;
    }

    /**
     * @brief Check if any results are expected
     *
     * Ignored results count as expected results. Use is_waiting_result() to see if a result is expected that
     * should be routed to a client.
     *
     * @return True if no results are expected
     */
    bool is_idle() const
    {
        return m_responses.empty();
    }

    /**
     * @brief Check if the server is a master
     *
     * @return True if server is a master
     */
    inline bool is_master() const
    {
        return m_target->is_master();
    }

    /**
     * @brief Check if the server is a slave
     *
     * @return True if the server is a slave
     */
    inline bool is_slave() const
    {
        return m_target->is_slave();
    }

    /**
     * @brief Check if the server is a relay server
     *
     * @return True if the server is a relay server
     */
    inline bool is_relay() const
    {
        return m_target->is_relay();
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
        return m_state == FATAL_FAILURE;
    }

    /**
     * @brief Get the object name of this server
     *
     * @return The unique object name of this server
     */
    inline const char* name() const
    {
        return m_target->name();
    }

    virtual void select_started();
    virtual void select_finished();

    int64_t                            num_selects() const;
    const maxbase::EpollIntervalTimer& select_timer() const;

private:
    /**
     * Internal state of the backend
     */
    enum backend_state
    {
        CLOSED,         /**< Backend is not in use*/
        IN_USE,         /**< Backend has been taken into use */
        FATAL_FAILURE,  /**< Backend references that should be dropped */
    };

    mxs::Endpoint* m_backend {nullptr};     /**< Backend server */
    mxs::Target*   m_target{nullptr};
    backend_state  m_state {CLOSED};        /**< State of the backend */

    mxb::EpollIntervalTimer m_select_timer;
    int64_t                 m_num_selects {0};

    // Contains the types of responses we're expecting from this backend. Used to detect if multiple commands
    // were sent to the backend but not all of the results should be sent to the client.
    mxb::small_vector<response_type> m_responses;
};
}
