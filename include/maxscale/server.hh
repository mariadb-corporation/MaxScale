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

#include <maxscale/ccdefs.hh>

#include <mutex>
#include <string>
#include <unordered_map>
#include <maxbase/average.hh>
#include <maxscale/ssl.hh>

/**
 * Server configuration parameters names
 */
extern const char CN_MONITORPW[];
extern const char CN_MONITORUSER[];
extern const char CN_PERSISTMAXTIME[];
extern const char CN_PERSISTPOOLMAX[];
extern const char CN_PROXY_PROTOCOL[];

/**
 * Status bits in the SERVER->status member, which describes the general state of a server. Although the
 * individual bits are independent, not all combinations make sense or are used. The bitfield is 64bits wide.
 */
// Bits used by most monitors
#define SERVER_RUNNING              (1 << 0)   /**<< The server is up and running */
#define SERVER_MAINT                (1 << 1)   /**<< Server is in maintenance mode */
#define SERVER_AUTH_ERROR           (1 << 2)   /**<< Authentication error from monitor */
#define SERVER_MASTER               (1 << 3)   /**<< The server is a master, i.e. can handle writes */
#define SERVER_SLAVE                (1 << 4)   /**<< The server is a slave, i.e. can handle reads */
#define SERVER_BEING_DRAINED        (1 << 5)   /**<< The server is being drained, i.e. no new connection should be created. */
#define SERVER_DISK_SPACE_EXHAUSTED (1 << 6)   /**<< The disk space of the server is exhausted */
// Bits used by MariaDB Monitor (mostly)
#define SERVER_SLAVE_OF_EXT_MASTER  (1 << 16)  /**<< Server is slave of a non-monitored master */
#define SERVER_RELAY                (1 << 17)  /**<< Server is a relay */
#define SERVER_WAS_MASTER           (1 << 18)  /**<< Server was a master but lost all slaves. */
// Bits used by other monitors
#define SERVER_JOINED               (1 << 19)  /**<< The server is joined in a Galera cluster */
#define SERVER_NDB                  (1 << 20)  /**<< The server is part of a MySQL cluster setup */
#define SERVER_MASTER_STICKINESS    (1 << 21)  /**<< Server Master stickiness */

inline bool status_is_connectable(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MAINT | SERVER_BEING_DRAINED)) == SERVER_RUNNING;
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

inline bool status_is_being_drained(uint64_t status)
{
    return status & SERVER_BEING_DRAINED;
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

inline bool status_is_joined(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_JOINED | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_JOINED);
}

inline bool status_is_ndb(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_NDB | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_NDB);
}

inline bool status_is_slave_of_ext_master(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER))
           == (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER);
}

inline bool status_is_disk_space_exhausted(uint64_t status)
{
    return status & SERVER_DISK_SPACE_EXHAUSTED;
}

/**
 * The SERVER structure defines a backend server. Each server has a name
 * or IP address for the server, a port that the server listens on and
 * the name of a protocol module that is loaded to implement the protocol
 * between the gateway and the server.
 */
class SERVER
{
public:
    static const int MAX_ADDRESS_LEN = 1024;
    static const int MAX_MONUSER_LEN = 512;
    static const int MAX_MONPW_LEN = 512;
    static const int MAX_VERSION_LEN = 256;
    static const int RLAG_UNDEFINED = -1;   // Default replication lag value

    /**
     * Maintenance mode request constants.
     */
    static const int MAINTENANCE_OFF = -100;
    static const int MAINTENANCE_NO_CHANGE = 0;
    static const int MAINTENANCE_ON = 100;
    static const int MAINTENANCE_FLAG_NOCHECK = 0;
    static const int MAINTENANCE_FLAG_CHECK = -1;

    // A mapping from a path to a percentage, e.g.: "/disk" -> 80.
    typedef std::unordered_map<std::string, int32_t> DiskSpaceLimits;

    enum class Type
    {
        MARIADB,
        MYSQL,
        CLUSTRIX
    };

    enum class RLagState
    {
        NONE,
        BELOW_LIMIT,
        ABOVE_LIMIT
    };

    struct Version
    {
        uint64_t total = 0;     /**< The version number received from server */
        uint32_t major = 0;     /**< Major version */
        uint32_t minor = 0;     /**< Minor version */
        uint32_t patch = 0;     /**< Patch version */
    };

    /* Server connection and usage statistics */
    struct ConnStats
    {
        int      n_connections = 0; /**< Number of connections */
        int      n_current = 0;     /**< Current connections */
        int      n_current_ops = 0; /**< Current active operations */
        int      n_persistent = 0;  /**< Current persistent pool */
        uint64_t n_new_conn = 0;    /**< Times the current pool was empty */
        uint64_t n_from_pool = 0;   /**< Times when a connection was available from the pool */
        uint64_t packets = 0;       /**< Number of packets routed to this server */
    };

    // Base settings
    char address[MAX_ADDRESS_LEN + 1] = {'\0'}; /**< Server hostname/IP-address */
    int  port = -1;                             /**< Server port */
    int  extra_port = -1;                       /**< Alternative monitor port if normal port fails */

    // Other settings
    bool proxy_protocol = false;    /**< Send proxy-protocol header to backends when connecting
                                     * routing sessions. */

    // Base variables
    bool          is_active = false;        /**< Server is active and has not been "destroyed" */
    SSL_LISTENER* server_ssl = nullptr;     /**< SSL data */
    uint8_t       charset = DEFAULT_CHARSET;/**< Character set. Read from backend and sent to client. */

    // Statistics and events
    ConnStats stats;            /**< The server statistics, e.g. number of connections */
    int       persistmax = 0;   /**< Maximum pool size actually achieved since startup */
    int       last_event = 0;   /**< The last event that occurred on this server */
    int64_t   triggered_at = 0; /**< Time when the last event was triggered */

    // Status descriptors. Updated automatically by a monitor or manually by the admin
    uint64_t status = 0;                            /**< Current status flag bitmap */

    long          node_id = -1;         /**< Node id, server_id for M/S or local_index for Galera */
    long          master_id = -1;       /**< Master server id of this node */
    int           rlag = RLAG_UNDEFINED;/**< Replication Lag for Master/Slave replication */
    unsigned long node_ts = 0;          /**< Last timestamp set from M/S monitor module */

    // Misc fields
    bool master_err_is_logged = false;      /**< If node failed, this indicates whether it is logged. Only
                                             * used by rwsplit. TODO: Move to rwsplit */
    bool warn_ssl_not_enabled = true;       /**< SSL not used for an SSL enabled server */
    RLagState rlag_state = RLagState::NONE; /**< Is replication lag above or under limit? Used by rwsplit. */

    virtual ~SERVER() = default;

    /**
     * Check if server has disk space threshold settings.
     *
     * @return True if limits exist
     */
    virtual bool have_disk_space_limits() const = 0;

    /**
     * Get a copy of disk space limit settings.
     *
     * @return A copy of settings
     */
    virtual DiskSpaceLimits get_disk_space_limits() const = 0;

    /**
     * Is persistent connection pool enabled.
     *
     * @return True if enabled
     */
    virtual bool persistent_conns_enabled() const = 0;

    /**
     * Fetch value of custom parameter.
     *
     * @param name Parameter name
     * @return Value of parameter, or empty if not found
     */
    virtual std::string get_custom_parameter(const std::string& name) const = 0;

    /**
     * Update server version.
     *
     * @param version_num New numeric version
     * @param version_str New version string
     */
    virtual void set_version(uint64_t version_num, const std::string& version_str) = 0;

    /**
     * Get numeric version information.
     *
     * @return Major, minor and patch numbers
     */
    virtual Version version() const = 0;

    /**
     * Get the type of the server.
     *
     * @return Server type
     */
    virtual Type type() const = 0;

    /**
     * Get version string.
     *
     * @return Version string
     */
    virtual std::string version_string() const = 0;

    /**
     * Returns the server configuration name. The value is returned as a c-string for printing convenience.
     *
     * @return Server name
     */
    virtual const char* name() const = 0;

    /**
     * Get backend protocol module name.
     *
     * @return Backend protocol module name of the server
     */
    virtual std::string protocol() const = 0;

    /*
     * Update server address. TODO: Move this to internal class once blr is gone.
     *
     * @param address       The new address
     */
    bool server_update_address(const std::string& address);

    /**
     * Update the server port. TODO: Move this to internal class once blr is gone.
     *
     * @param new_port New port. The value is not checked but should generally be 1 -- 65535.
     */
    void update_port(int new_port);

    /**
     * Update the server extra port. TODO: Move this to internal class once blr is gone.
     *
     * @param new_port New port. The value is not checked but should generally be 1 -- 65535.
     */
    void update_extra_port(int new_port);

    /**
     * @brief Check if a server points to a local MaxScale service
     *
     * @return True if the server points to a local MaxScale service
     */
    bool is_mxs_service();

    /**
     * Is the server valid and active? TODO: Rename once "is_active" is moved to internal class.
     *
     * @return True if server has not been removed from the runtime configuration.
     */
    bool server_is_active() const
    {
        return is_active;
    }

    /**
     * Is the server running and can be connected to?
     *
     * @return True if the server can be connected to.
     */
    bool is_connectable() const
    {
        return status_is_connectable(status);
    }

    /**
     * Is the server running and not in maintenance?
     *
     * @return True if server can be used.
     */
    bool is_usable() const
    {
        return status_is_usable(status);
    }

    /**
     * Is the server running?
     *
     * @return True if monitor can connect to the server.
     */
    bool is_running() const
    {
        return status_is_running(status);
    }

    /**
     * Is the server down?
     *
     * @return True if monitor cannot connect to the server.
     */
    bool is_down() const
    {
        return status_is_down(status);
    }

    /**
     * Is the server in maintenance mode?
     *
     * @return True if server is in maintenance.
     */
    bool is_in_maint() const
    {
        return status_is_in_maint(status);
    }

    /**
     * Is the server being drained?
     *
     * @return True if server is being drained.
     */
    bool is_being_drained() const
    {
        return status_is_being_drained(status);
    }

    /**
     * Is the server a master?
     *
     * @return True if server is running and marked as master.
     */
    bool is_master() const
    {
        return status_is_master(status);
    }

    /**
     * Is the server a slave.
     *
     * @return True if server is running and marked as slave.
     */
    bool is_slave() const
    {
        return status_is_slave(status);
    }

    /**
     * Is the server a relay slave?
     *
     * @return True, if server is a running relay.
     */
    bool is_relay() const
    {
        return status_is_relay(status);
    }

    /**
     * Is the server joined Galera node?
     *
     * @return True, if server is running and joined.
     */
    bool is_joined() const
    {
        return status_is_joined(status);
    }

    /**
     * Is the server a SQL node in MySQL Cluster?
     *
     * @return True, if server is running and with NDB status.
     */
    bool is_ndb() const
    {
        return status_is_ndb(status);
    }

    bool is_in_cluster() const
    {
        return (status & (SERVER_MASTER | SERVER_SLAVE | SERVER_RELAY | SERVER_JOINED | SERVER_NDB)) != 0;
    }

    bool is_slave_of_ext_master() const
    {
        return status_is_slave_of_ext_master(status);
    }

    bool is_low_on_disk_space() const
    {
        return status_is_disk_space_exhausted(status);
    }

    /**
     * Find a server with the specified name.
     *
     * @param name Name of the server
     * @return The server or NULL if not found
     */
    static SERVER* find_by_unique_name(const std::string& name);

    /**
     * Find several servers with the names specified in an array with a given size.
     * The returned array (but not the elements) should be freed by the caller.
     * If no valid server names were found or in case of error, nothing is written
     * to the output parameter.
     *
     * @param servers An array of server names
     * @param size Number of elements in the input server names array, equal to output
     * size if any servers are found.
     * @param output Where to save the output. Contains null elements for invalid server
     * names. If all were invalid, the output is left untouched.
     * @return Number of valid server names found
     */
    static int server_find_by_unique_names(char** server_names, int size, SERVER*** output);

    /**
     * Convert the current server status flags to a string.
     *
     * @param server The server to return the status for
     * @return A string representation of the status
     */
    std::string status_string() const;

    /**
     * Convert a set of server status flags to a string.
     *
     * @param flags Status flags
     * @return A string representation of the status flags
     */
    static std::string status_to_string(uint64_t flags);

    /**
     * Convert a status string to a status bit. Only converts one status element.
     *
     * @param str   String representation
     * @return bit value or 0 on error
     */
    static uint64_t status_from_string(const char* str);

    /**
     * Set a status bit in the server without locking
     *
     * @param bit           The bit to set for the server
     */
    void set_status(uint64_t bit);

    /**
     * Clear a status bit in the server without locking
     *
     * @param bit           The bit to clear for the server
     */
    void clear_status(uint64_t bit);

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

protected:
    SERVER()
    : m_response_time(maxbase::EMAverage {0.04, 0.35, 500})
    {
    }
private:
    static const int   DEFAULT_CHARSET = 0x08;   /**< The latin1 charset */
    maxbase::EMAverage m_response_time;          /**< Response time calculations for this server */
    std::mutex         m_average_write_mutex;    /**< Protects response time from concurrent writing */
};

namespace maxscale
{

/**
 * Set a status bit in the server. This should not be called from within a monitor. If the server is
 * monitored, only set the pending bit.
 *
 * @param bit           The bit to set for the server
 * @param errmsg_out    Error output
 */
bool server_set_status(SERVER* server, int bit, std::string* errmsg_out = NULL);

/**
 * Clear a status bit in the server under a lock. This ensures synchronization
 * with the server monitor thread. Calling this inside the monitor will likely
 * cause a deadlock. If the server is monitored, only clear the pending bit.
 *
 * @param bit           The bit to clear for the server
 * @param errmsg_out    Error output
 */
bool server_clear_status(SERVER* server, int bit, std::string* errmsg_out = NULL);

}
