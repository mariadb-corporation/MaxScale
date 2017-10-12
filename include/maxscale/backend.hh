#pragma once
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

#include <maxscale/cppdefs.hh>

#include <list>
#include <string>
#include <tr1/memory>

#include <maxscale/service.h>
#include <maxscale/session_command.hh>


namespace maxscale
{


class Backend
{
    Backend(const Backend&);
    Backend& operator =(const Backend&);
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
        EXPECT_RESPONSE,
        NO_RESPONSE
    };

    /**
     * @brief Create new Backend
     *
     * @param ref Server reference used by this backend
     */
    Backend(SERVER_REF* ref);

    virtual ~Backend();

    /**
     * @brief Execute the next session command in the queue
     *
     * @return True if the command was executed successfully
     */
    virtual bool execute_session_command();

    /**
     * @brief Add a new session command to the tail of the command queue
     *
     * @param buffer   Session command to add
     * @param sequence Sequence identifier of this session command, returned when
     *                 the session command is completed
     */
    void append_session_command(GWBUF* buffer, uint64_t sequence);
    void append_session_command(const SSessionCommand& sescmd);
    void append_session_command(const SessionCommandList& sescmdlist);

    /**
     * @brief Mark the current session command as successfully executed
     *
     * This should be called when the response to the command is received
     *
     * @return The sequence identifier for this session command
     */
    uint64_t complete_session_command();

    /**
     * @brief Check if backend has session commands
     *
     * @return Number of session commands
     */
    size_t session_command_count() const;

    /**
     * @brief Get the first session command
     *
     * Returns the first session command in the list of session commands
     * to be executed.
     *
     * This should only be called when at least one session command has been
     * added to the backend. If no session commands have been added, behavior
     * is undefined.
     *
     * @return The first session command
     */
    const SSessionCommand& next_session_command() const;

    /**
     * @brief Get pointer to server reference
     *
     * @return Pointer to server reference
     */
    inline SERVER_REF* backend() const
    {
        ss_dassert(m_backend);
        return m_backend;
    }

    /**
     * @brief Get pointer to server
     *
     * @return Pointer to server
     */
    inline SERVER* server() const
    {
        ss_dassert(m_backend);
        return m_backend->server;
    }

    /**
     * @brief Check if a connection to this backend can be made
     *
     * @return True if the backend has not failed and a connection can be attempted
     */
    inline bool can_connect() const
    {
        return !has_failed() && SERVER_IS_RUNNING(m_backend->server);
    }

    /**
     * @brief Create a new connection
     *
     * @param session The session to which the connection is linked
     *
     * @return True if connection was successfully created
     */
    bool connect(MXS_SESSION* session);

    /**
     * @brief Close the backend
     *
     * This will close all active connections created by the backend.
     */
    void close(close_type type = CLOSE_NORMAL);

    /**
     * @brief Get a pointer to the internal DCB
     *
     * @return Pointer to internal DCB
     */
    inline DCB* dcb() const
    {
        return m_dcb;
    }

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
     * @brief Write an authentication switch request to the backend server
     *
     * @param buffer Buffer containing the authentication switch request
     *
     * @return True if request was successfully written
     */
    bool auth(GWBUF* buffer);

    /**
     * @brief Mark that a reply to a query was received and processed
     */
    void ack_write();

    /**
     * @brief Store a command
     *
     * The command is stored and executed once the session can execute
     * the next command.
     *
     * @param buffer Buffer to store
     */
    void store_command(GWBUF* buffer);

    /**
     * @brief Write the stored command to the backend server
     *
     * @return True if command was written successfully
     */
    bool write_stored_command();

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
        return SERVER_REF_IS_ACTIVE(m_backend);
    }

    /**
     * @brief Check if backend is waiting for a result
     *
     * @return True if backend is waiting for a result
     */
    inline bool is_waiting_result() const
    {
        return m_state & WAITING_RESULT;
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
        return SERVER_IS_MASTER(m_backend->server);
    }

    /**
     * @brief Check if the server is a slave
     *
     * @return True if the server is a slave
     */
    inline bool is_slave() const
    {
        return SERVER_IS_SLAVE(m_backend->server);
    }

    /**
     * @brief Check if the server is a relay server
     *
     * @return True if the server is a relay server
     */
    inline bool is_relay() const
    {
        return SERVER_IS_RELAY_SERVER(m_backend->server);
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
        return m_backend->server->unique_name;
    }

    /**
     * @brief Get the address and port as a string
     *
     * @return The address and port combined into one string
     */
    inline const char* uri() const
    {
        return m_uri.c_str();
    }

private:
    /**
     * Internal state of the backend
     */
    enum backend_state
    {
        IN_USE           = 0x01, /**< Backend has been taken into use */
        WAITING_RESULT   = 0x02, /**< Waiting for a reply */
        FATAL_FAILURE    = 0x04  /**< Backend references that should be dropped */
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


    bool               m_closed;           /**< True if a connection has been opened and closed */
    SERVER_REF*        m_backend;          /**< Backend server */
    DCB*               m_dcb;              /**< Backend DCB */
    mxs::Buffer        m_pending_cmd;      /**< Pending commands */
    int                m_state;            /**< State of the backend */
    SessionCommandList m_session_commands; /**< List of session commands that are
                                            * to be executed on this backend server */
    std::string        m_uri;              /**< The combined address and port */
};

typedef std::tr1::shared_ptr<Backend> SBackend;
typedef std::list<SBackend> BackendList;
}
