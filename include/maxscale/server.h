#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
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
#include <maxscale/dcb.h>
#include <maxscale/resultset.h>

MXS_BEGIN_DECLS

#define MAX_SERVER_NAME_LEN 1024
#define MAX_SERVER_MONUSER_LEN 512
#define MAX_SERVER_MONPW_LEN 512
#define MAX_NUM_SLAVES 128 /**< Maximum number of slaves under a single server*/

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
} SERVER_STATS;

/**
 * The SERVER structure defines a backend server. Each server has a name
 * or IP address for the server, a port that the server listens on and
 * the name of a protocol module that is loaded to implement the protocol
 * between the gateway and the server.
 */
typedef struct server
{
#if defined(SS_DEBUG)
    skygw_chk_t     server_chk_top;
#endif
    SPINLOCK       lock;           /**< Common access lock */
    char           *unique_name;   /**< Unique name for the server */
    char           name[MAX_SERVER_NAME_LEN]; /**< Server name/IP address*/
    unsigned short port;           /**< Port to listen on */
    char           *protocol;      /**< Protocol module to use */
    char           *authenticator; /**< Authenticator module name */
    void           *auth_instance; /**< Authenticator instance */
    char           *auth_options;  /**< Authenticator options */
    SSL_LISTENER   *server_ssl;    /**< SSL data structure for server, if any */
    unsigned int   status;         /**< Status flag bitmap for the server */
    unsigned int   status_pending; /**< Pending status flag bitmap for the server */
    char           monuser[MAX_SERVER_MONUSER_LEN]; /**< User name to use to monitor the db */
    char           monpw[MAX_SERVER_MONPW_LEN]; /**< Password to use to monitor the db */
    SERVER_STATS   stats;          /**< The server statistics */
    struct  server *next;          /**< Next server */
    struct  server *nextdb;        /**< Next server in list attached to a service */
    char           *server_string; /**< Server version string, i.e. MySQL server version */
    long           node_id;        /**< Node id, server_id for M/S or local_index for Galera */
    int            rlag;           /**< Replication Lag for Master / Slave replication */
    unsigned long  node_ts;        /**< Last timestamp set from M/S monitor module */
    SERVER_PARAM   *parameters;    /**< Parameters of a server that may be used to weight routing decisions */
    long           master_id;      /**< Master server id of this node */
    int            depth;          /**< Replication level in the tree */
    long           slaves[MAX_NUM_SLAVES]; /**< Slaves of this node */
    bool           master_err_is_logged; /*< If node failed, this indicates whether it is logged */
    DCB            **persistent;    /**< List of unused persistent connections to the server */
    long           persistpoolmax; /**< Maximum size of persistent connections pool */
    long           persistmaxtime; /**< Maximum number of seconds connection can live */
    int            persistmax;     /**< Maximum pool size actually achieved since startup */
    uint8_t        charset;        /**< Default server character set */
    bool           is_active;      /**< Server is active and has not been "destroyed" */
    bool           created_online; /**< Whether this server was created after startup */
    struct
    {
        bool ssl_not_enabled; /**< SSL not used for an SSL enabled server */
    } log_warning; /**< Whether a specific warning was logged */
#if defined(SS_DEBUG)
    skygw_chk_t    server_chk_tail;
#endif
} SERVER;

enum
{
    MAX_RLAG_NOT_AVAILABLE = -1,
    MAX_RLAG_UNDEFINED = -2
};

/**
 * Status bits in the server->status member.
 *
 * These are a bitmap of attributes that may be applied to a server
 */
#define SERVER_RUNNING           0x0001  /**<< The server is up and running */
#define SERVER_MASTER            0x0002  /**<< The server is a master, i.e. can handle writes */
#define SERVER_SLAVE             0x0004  /**<< The server is a slave, i.e. can handle reads */
#define SERVER_JOINED            0x0008  /**<< The server is joined in a Galera cluster */
#define SERVER_NDB               0x0010  /**<< The server is part of a MySQL cluster setup */
#define SERVER_MAINT             0x0020  /**<< Server is in maintenance mode */
#define SERVER_SLAVE_OF_EXTERNAL_MASTER  0x0040 /**<< Server is slave of a Master outside
                                                   the provided replication topology */
#define SERVER_STALE_STATUS      0x0080  /**<< Server stale status, monitor didn't update it */
#define SERVER_MASTER_STICKINESS 0x0100  /**<< Server Master stickiness */
#define SERVER_AUTH_ERROR        0x1000  /**<< Authentication error from monitor */
#define SERVER_STALE_SLAVE       0x2000  /**<< Slave status is possible even without a master */
#define SERVER_RELAY_MASTER      0x4000  /**<< Server is a relay master */

/**
 * Is the server valid and active
 */
#define SERVER_IS_ACTIVE(server) (server->is_active)

/**
 * Is the server running - the macro returns true if the server is marked as running
 * regardless of it's state as a master or slave
 */
#define SERVER_IS_RUNNING(server) (((server)->status & (SERVER_RUNNING|SERVER_MAINT)) == SERVER_RUNNING)
/**
 * Is the server marked as down - the macro returns true if the server is believed
 * to be inoperable.
 */
#define SERVER_IS_DOWN(server)          (((server)->status & SERVER_RUNNING) == 0)
/**
 * Is the server a master? The server must be both running and marked as master
 * in order for the macro to return true
 */
#define SERVER_IS_MASTER(server) SRV_MASTER_STATUS((server)->status)

#define SRV_MASTER_STATUS(status) ((status &                            \
                                    (SERVER_RUNNING|SERVER_MASTER|SERVER_MAINT)) == \
                                   (SERVER_RUNNING|SERVER_MASTER))

/**
 * Is the server valid candidate for root master. The server must be running,
 * marked as master and not have maintenance bit set.
 */
#define SERVER_IS_ROOT_MASTER(server)                                   \
    (((server)->status & (SERVER_RUNNING|SERVER_MASTER|SERVER_MAINT)) == (SERVER_RUNNING|SERVER_MASTER))

/**
 * Is the server a slave? The server must be both running and marked as a slave
 * in order for the macro to return true
 */
#define SERVER_IS_SLAVE(server)                                         \
    (((server)->status & (SERVER_RUNNING|SERVER_SLAVE|SERVER_MAINT)) == \
     (SERVER_RUNNING|SERVER_SLAVE))

/**
 * Is the server joined Galera node? The server must be running and joined.
 */
#define SERVER_IS_JOINED(server)                                        \
    (((server)->status & (SERVER_RUNNING|SERVER_JOINED|SERVER_MAINT)) == (SERVER_RUNNING|SERVER_JOINED))

/**
 * Is the server a SQL node in MySQL Cluster? The server must be running and with NDB status
 */
#define SERVER_IS_NDB(server)                                           \
    (((server)->status & (SERVER_RUNNING|SERVER_NDB|SERVER_MAINT)) == (SERVER_RUNNING|SERVER_NDB))

/**
 * Is the server in maintenance mode.
 */
#define SERVER_IN_MAINT(server)         ((server)->status & SERVER_MAINT)

/** server is not master, slave or joined */
#define SERVER_NOT_IN_CLUSTER(s) (((s)->status & (SERVER_MASTER|SERVER_SLAVE|SERVER_JOINED|SERVER_NDB)) == 0)

#define SERVER_IS_IN_CLUSTER(s)  (((s)->status & (SERVER_MASTER|SERVER_SLAVE|SERVER_JOINED|SERVER_NDB)) != 0)

#define SERVER_IS_RELAY_SERVER(server)                                  \
    (((server)->status & (SERVER_RUNNING|SERVER_MASTER|SERVER_SLAVE|SERVER_MAINT)) == \
     (SERVER_RUNNING|SERVER_MASTER|SERVER_SLAVE))

#define SERVER_IS_SLAVE_OF_EXTERNAL_MASTER(s) (((s)->status & \
    (SERVER_RUNNING|SERVER_SLAVE_OF_EXTERNAL_MASTER)) == \
    (SERVER_RUNNING|SERVER_SLAVE_OF_EXTERNAL_MASTER))

/**
 * @brief Allocate a new server
 *
 * This will create a new server that represents a backend server that services
 * can use. This function will add the server to the running configuration but
 * will not persist the changes.
 *
 * @param name          Unique server name
 * @param address       The server address
 * @param port          The port to connect to
 * @param protocol      The protocol to use to connect to the server
 * @param authenticator The server authenticator module
 * @param auth_options  Options for the authenticator module
 * @return              The newly created server or NULL if an error occurred
 */
extern SERVER* server_alloc(const char *name, const char *address, unsigned short port,
                            const char *protocol, const char *authenticator,
                            const char *auth_options);

/**
 * @brief Find a server that can be reused
 *
 * A server that has been destroyed will not be deleted but only deactivated.
 *
 * @param name Name of the server
 * @param protocol Protocol used by the server
 * @param authenticator The authenticator module of the server
 * @param auth_options Options for the authenticator
 * @return Reusable SERVER or NULL if no servers matching the criteria were
 * found
 * @see runtime_create_server
 */
SERVER* server_find_destroyed(const char *name, const char *protocol,
                              const char *authenticator, const char *auth_options);
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
 * @brief Check if a server points to a local MaxScale service
 *
 * @param server Server to check
 * @return True if the server points to a local MaxScale service
 */
bool server_is_mxs_service(const SERVER *server);

extern int server_free(SERVER *server);
extern SERVER *server_find_by_unique_name(const char *name);
extern SERVER *server_find(const char *servname, unsigned short port);
extern char *server_status(const SERVER *);
extern void server_clear_set_status(SERVER *server, int specified_bits, int bits_to_set);
extern void server_set_status_nolock(SERVER *server, int bit);
extern void server_clear_status_nolock(SERVER *server, int bit);
extern void server_transfer_status(SERVER *dest_server, const SERVER *source_server);
extern void server_add_mon_user(SERVER *server, const char *user, const char *passwd);
extern const char *server_get_parameter(const SERVER *server, char *name);
extern void server_update_credentials(SERVER *server, const char *user, const char *passwd);
extern DCB* server_get_persistent(SERVER *server, const char *user, const char* ip, const char *protocol, int id);
extern void server_update_address(SERVER *server, const char *address);
extern void server_update_port(SERVER *server,  unsigned short port);
extern unsigned int server_map_status(const char *str);
extern bool server_set_version_string(SERVER* server, const char* string);
extern void server_set_status(SERVER *server, int bit);
extern void server_clear_status(SERVER *server, int bit);

extern void printServer(const SERVER *);
extern void printAllServers();
extern void dprintAllServers(DCB *);
extern void dprintAllServersJson(DCB *);
extern void dprintServer(DCB *, const SERVER *);
extern void dprintPersistentDCBs(DCB *, const SERVER *);
extern void dListServers(DCB *);
extern RESULTSET *serverGetList();

MXS_END_DECLS
