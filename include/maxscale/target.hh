/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <atomic>
#include <string>
#include <mutex>
#include <unordered_map>

#include <maxscale/modinfo.hh>
#include <maxscale/buffer.hh>
#include <maxbase/average.hh>

class MXS_SESSION;

constexpr int RANK_PRIMARY = 1;
constexpr int RANK_SECONDARY = 2;

/**
 * Status bits in the status() method, which describes the general state of a target. Although the
 * individual bits are independent, not all combinations make sense or are used. The bitfield is 64bits wide.
 */
// TODO: Rename with a different prefix or something
// Bits used by most monitors
#define SERVER_RUNNING              (1 << 0)    /**<< The server is up and running */
#define SERVER_MAINT                (1 << 1)    /**<< Server is in maintenance mode */
#define SERVER_AUTH_ERROR           (1 << 2)    /**<< Authentication error from monitor */
#define SERVER_MASTER               (1 << 3)    /**<< The server is a master, i.e. can handle writes */
#define SERVER_SLAVE                (1 << 4)    /**<< The server is a slave, i.e. can handle reads */
#define SERVER_DRAINING             (1 << 5)    /**<< The server is being drained, i.e. no new connection
                                                 * should be created. */
#define SERVER_DISK_SPACE_EXHAUSTED (1 << 6)    /**<< The disk space of the server is exhausted */

// Bits used by MariaDB Monitor (mostly)
#define SERVER_RELAY (1 << 11)                  /**<< Server is a relay */
#define SERVER_BLR   (1 << 12)                  /**<< Server is a replicating binlog router */
// Bits used by other monitors
#define SERVER_JOINED (1 << 20)                 /**<< The server is joined in a Galera cluster */

inline bool status_is_connectable(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MAINT | SERVER_DRAINING)) == SERVER_RUNNING;
}

inline bool status_is_usable(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MAINT)) == SERVER_RUNNING;
}

inline bool status_is_running(uint64_t status)
{
    return status & SERVER_RUNNING;
}

inline bool status_is_down(uint64_t status)
{
    return (status & SERVER_RUNNING) == 0;
}

inline bool status_is_in_maint(uint64_t status)
{
    return status & SERVER_MAINT;
}

inline bool status_is_draining(uint64_t status)
{
    return status & SERVER_DRAINING;
}

inline bool status_is_master(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MASTER | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_MASTER);
}

inline bool status_is_slave(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_SLAVE | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_SLAVE);
}

inline bool status_is_relay(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_RELAY | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_RELAY);
}

inline bool status_is_blr(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_BLR | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_BLR);
}

inline bool status_is_joined(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_JOINED | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_JOINED);
}

inline bool status_is_disk_space_exhausted(uint64_t status)
{
    return status & SERVER_DISK_SPACE_EXHAUSTED;
}

namespace maxscale
{

class Target;
class Reply;
class Endpoint;

// The route along which the reply arrived
using ReplyRoute = std::vector<Endpoint*>;
using Endpoints = std::vector<mxs::Endpoint*>;

// The type of error that handleError is dealing with
enum class ErrorType
{
    TRANSIENT,  // Temporary problem, Endpoint may be used again
    PERMANENT   // Systematic problem, Endpoint should not be used again
};

// A routing component
class Component
{
public:
    virtual ~Component() = default;

    virtual bool routeQuery(GWBUF* buffer) = 0;

    virtual bool clientReply(GWBUF* buffer, ReplyRoute& down, const mxs::Reply& reply) = 0;

    virtual bool handleError(ErrorType type, GWBUF* error, Endpoint* down, const mxs::Reply& reply) = 0;

    virtual void endpointConnReleased(Endpoint* down)
    {
    }
};

// A connectable routing endpoint (a service or a server)
class Endpoint : public Component
{
public:
    virtual ~Endpoint() = default;

    virtual bool connect() = 0;

    virtual void close() = 0;

    virtual bool is_open() const = 0;

    virtual mxs::Target* target() const = 0;

//
// Helper functions for storing a pointer to associated data
//
    void set_userdata(void* data)
    {
        m_data = data;
    }

    void* get_userdata()
    {
        return m_data;
    }

private:
    void* m_data {nullptr};
};

enum class RLagState
{
    NONE,
    BELOW_LIMIT,
    ABOVE_LIMIT
};

// A routing target
class Target
{
public:
    enum : int64_t {RLAG_UNDEFINED = -1};       // Default replication lag value
    enum : int64_t {PING_UNDEFINED = -1};       // Default ping value

    virtual ~Target() = default;

    /**
     * Get the target name
     *
     * The value is returned as a c-string for printing convenience.
     *
     * @return Target name
     */
    virtual const char* name() const = 0;

    /**
     * Get target status
     *
     * @return The status bitmask of the target
     */
    virtual uint64_t status() const = 0;

    /**
     * Is the target still active
     *
     * @return True if target is still active
     */
    virtual bool active() const = 0;

    /**
     * Get target rank
     */
    virtual int64_t rank() const = 0;

    /**
     * Returns the number of seconds that this target is behind in replication. If this target is a master or
     * replication lag is not applicable, returns -1.
     *
     * @return Replication lag
     */
    virtual int64_t replication_lag() const = 0;

    /**
     * Returns the latest replicated position that this target has reached.
     *
     * @param domain The replication domain to use
     *
     * @return The position the target is at or 0 if no events have been replicated from this domain
     */
    virtual uint64_t gtid_pos(uint32_t domain) const = 0;

    /**
     * Return ping in microseconds, or negative if the value is unknown (e.g. no connection).
     *
     * @return Ping in microseconds
     */
    virtual int64_t ping() const = 0;

    /**
     * Get the routing capabilities required by this target
     */
    virtual uint64_t capabilities() const = 0;

    /**
     * Get a connection handle to this target
     */
    virtual std::unique_ptr<Endpoint> get_connection(Component* up, MXS_SESSION* session) = 0;

    /**
     * Get children of this target
     *
     * @return A vector of targets that this target uses
     */
    virtual const std::vector<Target*>& get_children() const = 0;

    /* Target connection and usage statistics */
    class Stats
    {
    public:
        void    add_connection();
        void    remove_connection();
        int64_t n_current_conns() const;
        int64_t n_total_conns() const;

        int64_t add_conn_intent();
        void    remove_conn_intent();
        int64_t n_conn_intents() const;

        void    add_client_connection();
        void    remove_client_connection();
        int64_t n_client_conns() const;

        void add_failed_auth();
        void add_packet();

        void    add_current_op();
        void    remove_current_op();
        int64_t n_current_ops() const;

        json_t* to_json() const;

    private:
        using NumType = std::atomic_int64_t;

        NumType m_n_current_conns {0};  /**< Current number of connections */
        NumType m_n_total_conns {0};    /**< Total cumulative number of connections */
        NumType m_n_max_conns {0};      /**< Maximum instantaneous number of connections */
        NumType m_n_intended_conns {0}; /**< How many threads are intending on making a connection */

        NumType m_n_current_ops {0};    /**< Current number of active operations */
        NumType m_n_packets {0};        /**< Number of packets routed to this server */

        // The following only apply to services?
        NumType m_n_clients_conns {0};  /**< Current number of client connections */
        NumType m_failed_auths {0};     /**< Number of failed authentication attempts */
    };

    /**
     * Current server status as a string
     *
     * @return A string representation of the status
     */
    std::string status_string() const
    {
        return status_to_string(status(), stats().n_current_conns());
    }

    // Converts status bits to strings
    static std::string status_to_string(uint64_t flags, int n_connections);

    /**
     * Find a target by name
     *
     * @param name Name of the target to find
     *
     * @return The target or nullptr if target was not found
     */
    static Target* find(const std::string& name);

    /**
     * Get target statistics
     */
    const Stats& stats() const
    {
        return m_stats;
    }

    Stats& stats()
    {
        return m_stats;
    }

    /**
     * Is the target running and can be connected to?
     *
     * @return True if the target can be connected to.
     */
    bool is_connectable() const
    {
        return status_is_connectable(status());
    }

    /**
     * Is the target running and not in maintenance?
     *
     * @return True if target can be used.
     */
    bool is_usable() const
    {
        return status_is_usable(status());
    }

    /**
     * Is the target running?
     *
     * @return True if the target is running
     */
    bool is_running() const
    {
        return status_is_running(status());
    }

    /**
     * Is the target down?
     *
     * @return True if monitor cannot connect to the target.
     */
    bool is_down() const
    {
        return status_is_down(status());
    }

    /**
     * Is the target in maintenance mode?
     *
     * @return True if target is in maintenance.
     */
    bool is_in_maint() const
    {
        return status_is_in_maint(status());
    }

    /**
     * Is the target being drained?
     *
     * @return True if target is being drained.
     */
    bool is_draining() const
    {
        return status_is_draining(status());
    }

    /**
     * Is the target a master?
     *
     * @return True if target is running and marked as master.
     */
    bool is_master() const
    {
        return status_is_master(status());
    }

    /**
     * Is the target a slave.
     *
     * @return True if target is running and marked as slave.
     */
    bool is_slave() const
    {
        return status_is_slave(status());
    }

    /**
     * Is the target a relay slave?
     *
     * @return True, if target is a running relay.
     */
    bool is_relay() const
    {
        return status_is_relay(status());
    }

    /**
     * Is the target joined Galera node?
     *
     * @return True, if target is running and joined.
     */
    bool is_joined() const
    {
        return status_is_joined(status());
    }

    bool is_in_cluster() const
    {
        return (status() & (SERVER_MASTER | SERVER_SLAVE | SERVER_RELAY | SERVER_JOINED)) != 0;
    }

    bool is_low_on_disk_space() const
    {
        return status_is_disk_space_exhausted(status());
    }

    int response_time_num_samples() const
    {
        return m_response_time.num_samples();
    }

    double response_time_average() const
    {
        return m_response_time.average();
    }

    /**
     * Add a response time measurement to the global server value.
     *
     * @param ave The value to add
     * @param num_samples The weight of the new value, that is, the number of measurement points it represents
     */
    void response_time_add(double ave, int num_samples);

    /**
     * Set replication lag state
     *
     * @param new_state The new state
     * @param max_rlag  The replication lag limit
     */
    void set_rlag_state(RLagState new_state, int max_rlag);

protected:
    Stats              m_stats;
    maxbase::EMAverage m_response_time {0.04, 0.35, 500};   /**< Response time calculations for this server */
    std::mutex         m_average_write_mutex;               /**< Protects response time modifications */

    std::atomic<RLagState> m_rlag_state {RLagState::NONE};
};

class Error
{
public:
    Error() = default;

    // Returns true if an error has been set
    explicit operator bool() const;

    // True if the SQLSTATE is 40XXX: a rollback error
    bool is_rollback() const;

    // True if this was an error not in response to a query (connection killed, server shutdown)
    bool is_unexpected_error() const;

    // The error code
    uint32_t code() const;

    // The SQL state string (without the leading #)
    const std::string& sql_state() const;

    // The human readable error message
    const std::string& message() const;

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

    void clear();

private:
    uint16_t    m_code {0};
    std::string m_sql_state;
    std::string m_message;
};

enum class ReplyState
{
    START,          /**< Query sent to backend */
    DONE,           /**< Complete reply received */
    RSET_COLDEF,    /**< Resultset response, waiting for column definitions */
    RSET_COLDEF_EOF,/**< Resultset response, waiting for EOF for column definitions */
    RSET_ROWS,      /**< Resultset response, waiting for rows */
    PREPARE,        /**< COM_STMT_PREPARE response */
    LOAD_DATA,      /**< Sending data for LOAD DATA LOCAL INFILE */
    LOAD_DATA_END,  /**< Waiting for LOAD DATA LOCAL INFILE response */
};

class Reply
{
public:
    /**
     * Get a short human readable description of the reply
     */
    std::string describe() const;

    /**
     * Get the current state
     */
    ReplyState state() const;

    /**
     * Get state in string form
     */
    std::string to_string() const;

    /**
     * The command that the reply is for
     */
    uint8_t command() const;

    /**
     * Get latest error
     *
     * Evaluates to false if the response has no errors.
     *
     * @return The current error state.
     */
    const Error& error() const;

    /**
     * Check whether the response from the server is complete
     *
     * @return True if no more results are expected from this server
     */
    bool is_complete() const;

    /**
     * Check if a partial response has been received from the backend
     *
     * @return True if some parts of the reply have been received
     */
    bool has_started() const;

    /**
     * Is the reply a resultset?
     *
     * @return True if the reply is a resultset
     */
    bool is_resultset() const;

    /**
     * Does the current reply consist of only OK packets?
     *
     * This means that the returned reply has no resultsets or errors in it.
     *
     * @return True if the current reply consists of only OK packets
     */
    bool is_ok() const;

    /**
     * Number of rows read from the result
     */
    uint64_t rows_read() const;

    /**
     * Number of warnings returned
     */
    uint16_t num_warnings() const;

    /**
     * The latest status of the server, read from OK and EOF packets
     */
    uint16_t server_status() const;

    /**
     * Number of bytes received
     */
    uint64_t size() const;

    /**
     * The field counts for all received result sets
     */
    const std::vector<uint64_t>& field_counts() const;

    /**
     * The server-generated ID for a prepared statement if one was created
     */
    uint32_t generated_id() const;

    /**
     * The number of input parameters the prepared statement has
     */
    uint16_t param_count() const;

    /**
     * System variable state changes returned by the server
     *
     * @param name The variable name
     *
     * @return The variable value or an empty string if the variable was not set
     */
    std::string get_variable(const std::string& name) const;

    /**
     * Get rows returned in the result
     *
     * The rows can contain binary data. Don't rely on c_str() working correctly unless you know the result
     * doesn't have embedded nulls in it.
     *
     * @return The rows of the resultset if it returned any. If no rows were returned, an empty array is
     *         returned. To distinguish OK packets from empty results, use is_ok().
     */
    const std::vector<std::vector<std::string>>& row_data() const;

    //
    // Setters
    //

    void set_command(uint8_t command);

    void set_reply_state(mxs::ReplyState state);

    void add_rows(uint64_t row_count);

    void add_bytes(uint64_t size);

    void add_field_count(uint64_t field_count);

    void set_generated_id(uint32_t id);

    void set_param_count(uint16_t id);

    void set_is_ok(bool is_ok);

    void set_variable(const std::string& key, const std::string& value);

    void set_num_warnings(uint16_t warnings);

    void set_server_status(uint16_t status);

    void add_row_data(std::vector<std::string> row);

    void clear();

    template<typename ... Args>
    void set_error(Args... args)
    {
        m_error.set(std::forward<Args>(args)...);
    }

private:
    uint8_t               m_command {0};
    ReplyState            m_reply_state {ReplyState::DONE};
    Error                 m_error;
    uint64_t              m_row_count {0};
    uint64_t              m_size {0};
    uint32_t              m_generated_id {0};
    uint16_t              m_param_count {0};
    uint16_t              m_num_warnings {0};
    uint16_t              m_server_status {0};
    bool                  m_is_ok {false};
    std::vector<uint64_t> m_field_counts;

    std::unordered_map<std::string, std::string> m_variables;
    std::vector<std::vector<std::string>>        m_row_data;
};
}
