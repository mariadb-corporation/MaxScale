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

#include <string>
#include <maxscale/ssl.h>
#include <unordered_map>

// A mapping from a path to a percentage, e.g.: "/disk" -> 80.
typedef std::unordered_map<std::string, int32_t> MxsDiskSpaceThreshold;

/**
 * Server configuration parameters names
 */
extern const char CN_MONITORPW[];
extern const char CN_MONITORUSER[];
extern const char CN_PERSISTMAXTIME[];
extern const char CN_PERSISTPOOLMAX[];
extern const char CN_PROXY_PROTOCOL[];

/**
 * Maintenance mode request constants.
 */
const int MAINTENANCE_OFF = -100;
const int MAINTENANCE_NO_CHANGE = 0;
const int MAINTENANCE_ON = 100;
const int MAINTENANCE_FLAG_NOCHECK = 0;
const int MAINTENANCE_FLAG_CHECK = -1;

/* Server connection and usage statistics */
struct SERVER_STATS
{
    int      n_connections = 0; /**< Number of connections */
    int      n_current = 0;     /**< Current connections */
    int      n_current_ops = 0; /**< Current active operations */
    int      n_persistent = 0;  /**< Current persistent pool */
    uint64_t n_new_conn = 0;    /**< Times the current pool was empty */
    uint64_t n_from_pool = 0;   /**< Times when a connection was available from the pool */
    uint64_t packets = 0;       /**< Number of packets routed to this server */
};

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
    static const int MAX_MONUSER_LEN = 1024;
    static const int MAX_MONPW_LEN = 1024;
    static const int MAX_VERSION_LEN = 256;
    static const int RLAG_UNDEFINED = -1;   // Default replication lag value

    enum class Type
    {
        MARIADB,
        MYSQL,
        CLUSTRIX
    };

    struct Version
    {
        uint64_t total = 0; /**< The version number received from server */
        uint32_t major = 0; /**< Major version */
        uint32_t minor = 0; /**< Minor version */
        uint32_t patch = 0; /**< Patch version */
    };

    // Base settings
    char address[MAX_ADDRESS_LEN] = {'\0'};   /**< Server hostname/IP-address */
    int  port = -1;                           /**< Server port */
    int  extra_port = -1;                     /**< Alternative monitor port if normal port fails */

    // Other settings
    char monuser[MAX_MONUSER_LEN] = {'\0'};     /**< Monitor username, overrides monitor setting */
    char monpw[MAX_MONPW_LEN] = {'\0'};         /**< Monitor password, overrides monitor setting  */
    bool proxy_protocol = false;                /**< Send proxy-protocol header to backends when connecting
                                                 *   routing sessions. */

    // Base variables
    bool          is_active = false;        /**< Server is active and has not been "destroyed" */
    void*         auth_instance = nullptr;  /**< Authenticator instance data */
    SSL_LISTENER* server_ssl = nullptr;     /**< SSL data */
    uint8_t       charset = DEFAULT_CHARSET;/**< Character set. Read from backend and sent to client. */

    // Statistics and events
    SERVER_STATS stats;             /**< The server statistics, e.g. number of connections */
    int          persistmax = 0;    /**< Maximum pool size actually achieved since startup */
    int          last_event = 0;    /**< The last event that occurred on this server */
    int64_t      triggered_at = 0;  /**< Time when the last event was triggered */

    // Status descriptors. Updated automatically by a monitor or manually by the admin
    uint64_t status = 0;                                    /**< Current status flag bitmap */
    int      maint_request = MAINTENANCE_NO_CHANGE;         /**< Is admin requesting Maintenance=ON/OFF on the
                                                             * server? */

    long          node_id = -1;         /**< Node id, server_id for M/S or local_index for Galera */
    long          master_id = -1;       /**< Master server id of this node */
    int           rlag = RLAG_UNDEFINED;/**< Replication Lag for Master/Slave replication */
    unsigned long node_ts = 0;          /**< Last timestamp set from M/S monitor module */

    // Misc fields
    bool master_err_is_logged = false;  /**< If node failed, this indicates whether it is logged. Only
                                         * used by rwsplit. TODO: Move to rwsplit */
    bool warn_ssl_not_enabled = true;   /**< SSL not used for an SSL enabled server */

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
    virtual MxsDiskSpaceThreshold get_disk_space_limits() const = 0;

    /**
     * Set new disk space limits for the server.
     *
     * @param new_limits New limits
     */
    virtual void set_disk_space_limits(const MxsDiskSpaceThreshold& new_limits) = 0;

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

protected:
    SERVER()
    {
    }
private:
    static const int DEFAULT_CHARSET = 0x08;    /** The latin1 charset */
};

/**
 * Status bits in the SERVER->status member, which describes the general state of a server. Although the
 * individual bits are independent, not all combinations make sense or are used. The bitfield is 64bits wide.
 */
// Bits used by most monitors
#define SERVER_RUNNING    (1 << 0)              /**<< The server is up and running */
#define SERVER_MAINT      (1 << 1)              /**<< Server is in maintenance mode */
#define SERVER_AUTH_ERROR (1 << 2)              /**<< Authentication error from monitor */
#define SERVER_MASTER     (1 << 3)              /**<< The server is a master, i.e. can handle writes */
#define SERVER_SLAVE      (1 << 4)              /**<< The server is a slave, i.e. can handle reads */
// Bits used by MariaDB Monitor (mostly)
#define SERVER_SLAVE_OF_EXT_MASTER (1 << 5)     /**<< Server is slave of a non-monitored master */
#define SERVER_RELAY               (1 << 6)     /**<< Server is a relay */
#define SERVER_WAS_MASTER          (1 << 7)     /**<< Server was a master but lost all slaves. */
// Bits used by other monitors
#define SERVER_JOINED            (1 << 8)   /**<< The server is joined in a Galera cluster */
#define SERVER_NDB               (1 << 9)   /**<< The server is part of a MySQL cluster setup */
#define SERVER_MASTER_STICKINESS (1 << 10)  /**<< Server Master stickiness */
// Bits providing general information
#define SERVER_DISK_SPACE_EXHAUSTED (1 << 31)   /**<< The disk space of the server is exhausted */

/**
 * Is the server valid and active?
 *
 * @param server The server
 * @return True, if server has not been removed from the runtime configuration.
 */
inline bool server_is_active(const SERVER* server)
{
    return server->is_active;
}

inline bool status_is_usable(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MAINT)) == SERVER_RUNNING;
}

/**
 * Is the server running and not in maintenance?
 *
 * @param server The server
 * @return True, if server can be used.
 */
inline bool server_is_usable(const SERVER* server)
{
    return status_is_usable(server->status);
}

inline bool status_is_running(uint64_t status)
{
    return status & SERVER_RUNNING;
}

/**
 * Is the server running?
 *
 * @param server The server
 * @return True, if monitor can connect to the server.
 */
inline bool server_is_running(const SERVER* server)
{
    return status_is_running(server->status);
}

inline bool status_is_down(uint64_t status)
{
    return (status & SERVER_RUNNING) == 0;
}

/**
 * Is the server down?
 *
 * @param server The server
 * @return True, if monitor cannot connect to the server.
 */
inline bool server_is_down(const SERVER* server)
{
    return status_is_down(server->status);
}

inline bool status_is_in_maint(uint64_t status)
{
    return status & SERVER_MAINT;
}

/**
 * Is the server in maintenance mode?
 *
 * @param server The server
 * @return True, if server is in maintenance.
 */
inline bool server_is_in_maint(const SERVER* server)
{
    return status_is_in_maint(server->status);
}

inline bool status_is_master(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MASTER | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_MASTER);
}

/**
 * Is the server a master?
 *
 * @param server The server
 * @return True, if server is running and marked as master.
 */
inline bool server_is_master(const SERVER* server)
{
    return status_is_master(server->status);
}

inline bool status_is_slave(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_SLAVE | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_SLAVE);
}

/**
 * Is the server a slave.
 *
 * @param server The server
 * @return True if server is running and marked as slave.
 */
inline bool server_is_slave(const SERVER* server)
{
    return status_is_slave(server->status);
}

inline bool status_is_relay(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_RELAY | SERVER_MAINT))    \
           == (SERVER_RUNNING | SERVER_RELAY);
}

inline bool server_is_relay(const SERVER* server)
{
    return status_is_relay(server->status);
}

inline bool status_is_joined(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_JOINED | SERVER_MAINT))
           == (SERVER_RUNNING | SERVER_JOINED);
}

/**
 * Is the server joined Galera node? The server must be running and joined.
 */
inline bool server_is_joined(const SERVER* server)
{
    return status_is_joined(server->status);
}

inline bool status_is_ndb(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_NDB | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_NDB);
}

/**
 * Is the server a SQL node in MySQL Cluster? The server must be running and with NDB status
 */
inline bool server_is_ndb(const SERVER* server)
{
    return status_is_ndb(server->status);
}

inline bool server_is_in_cluster(const SERVER* server)
{
    return (server->status
            & (SERVER_MASTER | SERVER_SLAVE | SERVER_RELAY | SERVER_JOINED | SERVER_NDB)) != 0;
}

inline bool status_is_slave_of_ext_master(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER))
           == (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER);
}

inline bool server_is_slave_of_ext_master(const SERVER* server)
{
    return status_is_slave_of_ext_master(server->status);
}

inline bool status_is_disk_space_exhausted(uint64_t status)
{
    return status & SERVER_DISK_SPACE_EXHAUSTED;
}

inline bool server_is_disk_space_exhausted(const SERVER* server)
{
    return status_is_disk_space_exhausted(server->status);
}

/**
 * @brief Add a server parameter
 *
 * @param server Server where the parameter is added
 * @param name Parameter name
 * @param value Parameter value
 */
void server_add_parameter(SERVER* server, const char* name, const char* value);

/**
 * @brief Check if a server points to a local MaxScale service
 *
 * @param server Server to check
 * @return True if the server points to a local MaxScale service
 */
bool server_is_mxs_service(const SERVER* server);

/**
 * @brief Convert all servers into JSON format
 *
 * @param host    Hostname of this server
 *
 * @return JSON array of servers or NULL if an error occurred
 */
json_t* server_list_to_json(const char* host);

/**
 * @brief Set the disk space threshold of the server
 *
 * @param server                The server.
 * @param disk_space_threshold  The disk space threshold as specified in the config file.
 *
 * @return True, if the provided string is valid and the threshold could be set.
 */
bool server_set_disk_space_threshold(SERVER* server, const char* disk_space_threshold);

/**
 * @brief Add a response average to the server response average.
 *
 * @param server      The server.
 * @param ave         Average.
 * @param num_samples Number of samples the average consists of.
 *
 */
void server_add_response_average(SERVER* server, double ave, int num_samples);

extern int     server_free(SERVER* server);
extern SERVER* server_find_by_unique_name(const char* name);
extern int     server_find_by_unique_names(char** server_names, int size, SERVER*** output);
extern void    server_clear_set_status_nolock(SERVER* server, uint64_t bits_to_clear, uint64_t bits_to_set);
extern void    server_set_status_nolock(SERVER* server, uint64_t bit);
extern void    server_clear_status_nolock(SERVER* server, uint64_t bit);
extern void    server_transfer_status(SERVER* dest_server, const SERVER* source_server);
extern void    server_add_mon_user(SERVER* server, const char* user, const char* passwd);
extern void    server_update_credentials(SERVER* server, const char* user, const char* passwd);
extern void     server_update_address(SERVER* server, const char* address);
extern uint64_t server_map_status(const char* str);

extern void printServer(const SERVER*);
extern void printAllServers();

int    server_response_time_num_samples(const SERVER* server);
double server_response_time_average(const SERVER* server);

namespace maxscale
{
std::string server_status(uint64_t flags);
std::string server_status(const SERVER*);
bool        server_set_status(SERVER* server, int bit, std::string* errmsg_out = NULL);
bool        server_clear_status(SERVER* server, int bit, std::string* errmsg_out = NULL);
}
