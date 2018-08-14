#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * @file server.h
 *
 * The server level definitions within the gateway
 */

#include <maxscale/cdefs.h>
#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/jansson.h>

MXS_BEGIN_DECLS

#define MAX_SERVER_ADDRESS_LEN 1024
#define MAX_SERVER_MONUSER_LEN 1024
#define MAX_SERVER_MONPW_LEN   1024
#define MAX_SERVER_VERSION_LEN 256

#define MAX_NUM_SLAVES 128 /**< Maximum number of slaves under a single server*/

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
/**
 * The server parameters used for weighting routing decissions
 */
typedef struct server_params
{
    char *name;                 /**< Parameter name */
    char *value;                /**< Parameter value */
    bool active;                /**< Whether the parameter is valid */
    struct server_params *next; /**< Next Paramter in the linked list */
} SERVER_PARAM;

/**
 * The server statistics structure
 */
typedef struct
{
    int n_connections;    /**< Number of connections */
    int n_current;        /**< Current connections */
    int n_current_ops;    /**< Current active operations */
    int n_persistent;     /**< Current persistent pool */
    uint64_t n_new_conn;  /**< Times the current pool was empty */
    uint64_t n_from_pool; /**< Times when a connection was available from the pool */
    uint64_t packets;     /**< Number of packets routed to this server */
} SERVER_STATS;

/**
 * The server version.
 */
typedef struct server_version
{
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} SERVER_VERSION;

typedef enum
{
    SERVER_TYPE_MARIADB,
    SERVER_TYPE_MYSQL
} server_type_t;

static inline void server_decode_version(uint64_t version, SERVER_VERSION* server_version)
{
    uint32_t major = version / 10000;
    uint32_t minor = (version - major * 10000) / 100;
    uint32_t patch = version - major * 10000 - minor * 100;

    server_version->major = major;
    server_version->minor = minor;
    server_version->patch = patch;
}

static uint64_t server_encode_version(const SERVER_VERSION* server_version)
{
    return server_version->major * 10000 + server_version->minor * 100 + server_version->patch;
}

/**
 * The SERVER structure defines a backend server. Each server has a name
 * or IP address for the server, a port that the server listens on and
 * the name of a protocol module that is loaded to implement the protocol
 * between the gateway and the server.
 */
typedef struct server
{
    // Base settings
    char           *name;          /**< Server config name */
    char           address[MAX_SERVER_ADDRESS_LEN]; /**< Server hostname/IP-address */
    unsigned short port;           /**< Server port */
    char           *protocol;      /**< Backend protocol module name */
    char           *authenticator; /**< Authenticator module name */
    // Other settings
    char           monuser[MAX_SERVER_MONUSER_LEN]; /**< Monitor username, overrides monitor setting */
    char           monpw[MAX_SERVER_MONPW_LEN]; /**< Monitor password, overrides monitor setting  */
    long           persistpoolmax; /**< Maximum size of persistent connections pool */
    long           persistmaxtime; /**< Maximum number of seconds connection can live */
    bool           proxy_protocol; /**< Send proxy-protocol header to backends when connecting
                                    *   routing sessions. */
    SERVER_PARAM   *parameters;    /**< Additional custom parameters which may affect routing decisions. */
    // Base variables
    SPINLOCK       lock;           /**< Access lock. Required when modifying server status or settings. */
    bool           is_active;      /**< Server is active and has not been "destroyed" */
    void           *auth_instance; /**< Authenticator instance data */
    SSL_LISTENER   *server_ssl;    /**< SSL data */
    DCB            **persistent;   /**< List of unused persistent connections to the server */
    uint8_t        charset;        /**< Server character set. Read from backend and sent to client. */
    // Statistics and events
    SERVER_STATS   stats;          /**< The server statistics, e.g. number of connections */
    int            persistmax;     /**< Maximum pool size actually achieved since startup */
    int            last_event;     /**< The last event that occurred on this server */
    int64_t        triggered_at;   /**< Time when the last event was triggered */
    bool           active_event;   /**< Was MaxScale active when last event was observed */
    // Status descriptors. Updated automatically by a monitor or manually by the admin
    uint64_t       status;         /**< Current status flag bitmap */
    int            maint_request;  /**< Is admin requesting Maintenance=ON/OFF on the server? */
    char           version_string[MAX_SERVER_VERSION_LEN]; /**< Server version string as given by backend */
    uint64_t       version;        /**< Server version numeric representation */
    server_type_t  server_type;    /**< Server type (MariaDB or MySQL), deduced from version string */
    long           node_id;        /**< Node id, server_id for M/S or local_index for Galera */
    int            rlag;           /**< Replication Lag for Master/Slave replication */
    unsigned long  node_ts;        /**< Last timestamp set from M/S monitor module */
    long           master_id;      /**< Master server id of this node */
    // Misc fields
    bool           master_err_is_logged; /**< If node failed, this indicates whether it is logged. Only used
                                          *   by rwsplit. TODO: Move to rwsplit */
    bool           warn_ssl_not_enabled; /**< SSL not used for an SSL enabled server */
    MxsDiskSpaceThreshold* disk_space_threshold; /**< Disk space thresholds */
} SERVER;

enum
{
    MAX_RLAG_NOT_AVAILABLE = -1,
    MAX_RLAG_UNDEFINED = -2
};

/**
 * Status bits in the SERVER->status member, which describes the general state of a server. Although the
 * individual bits are independent, not all combinations make sense or are used. The bitfield is 64bits wide.
 */
// Bits used by most monitors
#define SERVER_RUNNING              (1 << 0)  /**<< The server is up and running */
#define SERVER_MAINT                (1 << 1)  /**<< Server is in maintenance mode */
#define SERVER_AUTH_ERROR           (1 << 2)  /**<< Authentication error from monitor */
#define SERVER_MASTER               (1 << 3)  /**<< The server is a master, i.e. can handle writes */
#define SERVER_SLAVE                (1 << 4)  /**<< The server is a slave, i.e. can handle reads */
// Bits used by MariaDB Monitor (mostly)
#define SERVER_SLAVE_OF_EXT_MASTER  (1 << 5)  /**<< Server is slave of a non-monitored master */
#define SERVER_RELAY                (1 << 6)  /**<< Server is a relay */
#define SERVER_WAS_MASTER           (1 << 7)  /**<< Server was a master but lost all slaves. */
// Bits used by other monitors
#define SERVER_JOINED               (1 << 8)  /**<< The server is joined in a Galera cluster */
#define SERVER_NDB                  (1 << 9) /**<< The server is part of a MySQL cluster setup */
#define SERVER_MASTER_STICKINESS    (1 << 10) /**<< Server Master stickiness */
// Bits providing general information
#define SERVER_DISK_SPACE_EXHAUSTED (1 << 31) /**<< The disk space of the server is exhausted */

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
    return (status & SERVER_RUNNING);
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
    return (status & (SERVER_RUNNING | SERVER_RELAY | SERVER_MAINT)) == \
            (SERVER_RUNNING | SERVER_RELAY);
}

inline bool server_is_relay(const SERVER* server)
{
    return status_is_relay(server->status);
}

inline bool status_is_joined(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_JOINED | SERVER_MAINT)) ==
            (SERVER_RUNNING | SERVER_JOINED);
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
    return ((server->status &
            (SERVER_MASTER | SERVER_SLAVE | SERVER_RELAY | SERVER_JOINED | SERVER_NDB)) != 0);
}

inline bool server_is_slave_of_ext_master(const SERVER* server)
{
    return ((server->status & (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER)) ==
            (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER));
}

inline bool status_is_disk_space_exhausted(uint64_t status)
{
    return (status & SERVER_DISK_SPACE_EXHAUSTED);
}

inline bool server_is_disk_space_exhausted(const SERVER* server)
{
    return status_is_disk_space_exhausted(server->status);
}

/**
 * @brief Allocate a new server
 *
 * This will create a new server that represents a backend server that services
 * can use. This function will add the server to the running configuration but
 * will not persist the changes.
 *
 * @param name   Unique server name
 * @param params Parameters for the server
 *
 * @return       The newly created server or NULL if an error occurred
 */
extern SERVER* server_alloc(const char *name, MXS_CONFIG_PARAMETER* params);

/**
 * @brief Find a server that can be reused
 *
 * A server that has been destroyed will not be deleted but only deactivated.
 *
 * @param name          Name of the server
 * @param protocol      Protocol used by the server
 * @param authenticator The authenticator module of the server
 * @param address       The network address of the new server
 * @param port          The port of the new server
 *
 * @return Repurposed SERVER or NULL if no servers matching the criteria were
 * found
 * @see runtime_create_server
 */
SERVER* server_repurpose_destroyed(const char *name, const char *protocol, const char *authenticator,
                                   const char *address, const char *port);
/**
 * @brief Serialize a server to a file
 *
 * This converts @c server into an INI format file. This allows created servers
 * to be persisted to disk. This will replace any existing files with the same
 * name.
 *
 * @param server Server to serialize
 * @return False if the serialization of the server fails, true if it was successful
 */
bool server_serialize(const SERVER *server);

/**
 * @brief Add a server parameter
 *
 * @param server Server where the parameter is added
 * @param name Parameter name
 * @param value Parameter value
 */
void server_add_parameter(SERVER *server, const char *name, const char *value);

/**
 * @brief Remove a server parameter
 *
 * @param server Server to remove the parameter from
 * @param name The name of the parameter to remove
 * @return True if a parameter was removed
 */
bool server_remove_parameter(SERVER *server, const char *name);

/**
 * @brief Update server parameter
 *
 * @param server Server to update
 * @param name   Parameter to update
 * @param value  New value of parameter
 */
void server_update_parameter(SERVER *server, const char *name, const char *value);

/**
 * @brief Check if a server points to a local MaxScale service
 *
 * @param server Server to check
 * @return True if the server points to a local MaxScale service
 */
bool server_is_mxs_service(const SERVER *server);

/**
 * @brief Convert a server to JSON format
 *
 * @param server Server to convert
 * @param host    Hostname of this server
 *
 * @return JSON representation of server or NULL if an error occurred
 */
json_t* server_to_json(const SERVER* server, const char* host);

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
bool server_set_disk_space_threshold(SERVER *server, const char *disk_space_threshold);

extern SERVER *server_find_by_unique_name(const char *name);
extern int server_find_by_unique_names(char **server_names, int size, SERVER*** output);
extern SERVER *server_find(const char *servname, unsigned short port);
extern char *server_status(const SERVER *);
extern void server_clear_set_status_nolock(SERVER *server, uint64_t bits_to_clear, uint64_t bits_to_set);
extern void server_set_status_nolock(SERVER *server, uint64_t bit);
extern void server_clear_status_nolock(SERVER *server, uint64_t bit);
extern void server_transfer_status(SERVER *dest_server, const SERVER *source_server);
extern void server_add_mon_user(SERVER *server, const char *user, const char *passwd);
extern size_t server_get_parameter(const SERVER *server, const char *name, char* out, size_t size);
extern void server_update_credentials(SERVER *server, const char *user, const char *passwd);
extern DCB* server_get_persistent(SERVER *server, const char *user, const char* ip, const char *protocol, int id);
extern void server_update_address(SERVER *server, const char *address);
extern void server_update_port(SERVER *server,  unsigned short port);
extern uint64_t server_map_status(const char *str);
extern void server_set_version_string(SERVER* server, const char* version_string);
extern void server_set_version(SERVER* server, const char* version_string, uint64_t version);
extern uint64_t server_get_version(const SERVER* server);

extern void printServer(const SERVER *);
extern void printAllServers();
extern void dprintAllServers(DCB *);
extern void dprintAllServersJson(DCB *);
extern void dprintServer(DCB *, const SERVER *);
extern void dprintPersistentDCBs(DCB *, const SERVER *);
extern void dListServers(DCB *);

MXS_END_DECLS
