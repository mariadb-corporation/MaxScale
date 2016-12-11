#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file service.h
 *
 * The service level definitions within the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 14/06/13     Mark Riddoch            Initial implementation
 * 18/06/13     Mark Riddoch            Addition of statistics and function
 *                                      prototypes
 * 23/06/13     Mark Riddoch            Added service user and users
 * 06/02/14     Massimiliano Pinto      Added service flag for root user access
 * 25/02/14     Massimiliano Pinto      Added service refresh limit feature
 * 07/05/14     Massimiliano Pinto      Added version_string field to service
 *                                      struct
 * 29/05/14     Mark Riddoch            Filter API mechanism
 * 26/06/14     Mark Riddoch            Added WeightBy support
 * 09/09/14     Massimiliano Pinto      Added service option for localhost authentication
 * 09/10/14     Massimiliano Pinto      Added service resources via hashtable
 * 31/05/16     Martin Brampton         Add fields to support connection throttling
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <time.h>
#include <maxscale/gw_protocol.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/server.h>
#include <maxscale/listener.h>
#include <maxscale/filter.h>
#include <maxscale/hashtable.h>
#include <maxscale/resultset.h>
#include <maxscale/config.h>
#include <maxscale/queuemanager.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

MXS_BEGIN_DECLS

struct server;
struct router;
struct router_object;
struct users;

/**
 * The service statistics structure
 */
typedef struct
{
    time_t started;         /**< The time when the service was started */
    int    n_failed_starts; /**< Number of times this service has failed to start */
    int    n_sessions;      /**< Number of sessions created on service since start */
    int    n_current;       /**< Current number of sessions */
} SERVICE_STATS;

/**
 * The service user structure holds the information that is needed
 for this service to allow the gateway to login to the backend
 database and extact information such as the user table or other
 database status or configuration data.
*/
typedef struct
{
    char *name;     /**< The user name to use to extract information */
    char *authdata; /**< The authentication data requied */
} SERVICE_USER;

/**
 * The service refresh rate holds the counter and last load time_t
 for this service to load users data from the backend database
*/
typedef struct
{
    int nloads;
    time_t last;
} SERVICE_REFRESH_RATE;

typedef struct server_ref_t
{
    struct server_ref_t *next; /**< Next server reference */
    SERVER* server;            /**< The actual server */
    int weight;                /**< Weight of this server */
    int connections;           /**< Number of connections created through this reference */
    bool active;               /**< Whether this reference is valid and in use*/
} SERVER_REF;

/** Macro to check whether a SERVER_REF is active */
#define SERVER_REF_IS_ACTIVE(ref) (ref->active && SERVER_IS_ACTIVE(ref->server))

#define SERVICE_MAX_RETRY_INTERVAL 3600 /*< The maximum interval between service start retries */

/** Value of service timeout if timeout checks are disabled */
#define SERVICE_NO_SESSION_TIMEOUT LONG_MAX

/**
 * Parameters that are automatically detected but can also be configured by the
 * user are initially set to this value.
 */
#define SERVICE_PARAM_UNINIT -1

/* Refresh rate limits for load users from database */
#define USERS_REFRESH_TIME         30 /* Allowed time interval (in seconds) after last update*/
#define USERS_REFRESH_MAX_PER_TIME 4  /* Max number of load calls within the time interval */

/** Default timeout values used by the connections which fetch user authentication data */
#define DEFAULT_AUTH_CONNECT_TIMEOUT 3
#define DEFAULT_AUTH_READ_TIMEOUT    1
#define DEFAULT_AUTH_WRITE_TIMEOUT   2

/**
 * Defines a service within the gateway.
 *
 * A service is a combination of a set of backend servers, a routing mechanism
 * and a set of client side protocol/port pairs used to listen for new connections
 * to the service.
 */
typedef struct service
{
    char *name;                        /**< The service name */
    int state;                         /**< The service state */
    int client_count;                  /**< Number of connected clients */
    int max_connections;               /**< Maximum client connections */
    QUEUE_CONFIG *queued_connections;  /**< Queued connections, if set */
    SERV_LISTENER *ports;              /**< Linked list of ports and protocols
                                        * that this service will listen on */
    char *routerModule;                /**< Name of router module to use */
    char **routerOptions;              /**< Router specific option strings */
    struct router_object *router;      /**< The router we are using */
    void *router_instance;             /**< The router instance for this service */
    char *version_string;              /**< version string for this service listeners */
    SERVER_REF *dbref;                 /**< server references */
    int         n_dbref;               /**< Number of server references */
    SERVICE_USER credentials;          /**< The cedentials of the service user */
    SPINLOCK spin;                     /**< The service spinlock */
    SERVICE_STATS stats;               /**< The service statistics */
    int enable_root;                   /**< Allow root user  access */
    int localhost_match_wildcard_host; /**< Match localhost against wildcard */
    CONFIG_PARAMETER* svc_config_param;/**<  list of config params and values */
    int svc_config_version;            /**<  Version number of configuration */
    bool svc_do_shutdown;              /**< tells the service to exit loops etc. */
    bool users_from_all;               /**< Load users from one server or all of them */
    bool strip_db_esc;                 /**< Remove the '\' characters from database names
                                        * when querying them from the server. MySQL Workbench seems
                                        * to escape at least the underscore character. */
    SPINLOCK users_table_spin;         /**< The spinlock for users data refresh */
    SERVICE_REFRESH_RATE rate_limit;   /**< The refresh rate limit for users table */
    FILTER_DEF **filters;              /**< Ordered list of filters */
    int n_filters;                     /**< Number of filters */
    long conn_idle_timeout;            /**< Session timeout in seconds */
    char *weightby;                    /**< Service weighting parameter name */
    struct service *next;              /**< The next service in the linked list */
    bool retry_start;                  /**< If starting of the service should be retried later */
    bool log_auth_warnings;            /**< Log authentication failures and warnings */
    uint64_t capabilities;             /**< The capabilities of the service. */
} SERVICE;

typedef enum count_spec_t
{
    COUNT_NONE = 0,
    COUNT_ATLEAST,
    COUNT_EXACT,
    COUNT_ATMOST
} count_spec_t;

#define SERVICE_STATE_ALLOC     1       /**< The service has been allocated */
#define SERVICE_STATE_STARTED   2       /**< The service has been started */
#define SERVICE_STATE_FAILED    3       /**< The service failed to start */
#define SERVICE_STATE_STOPPED   4       /**< The service has been stopped */

/**
 * Starting and stopping services
 */

/**
 * @brief Stop a service
 *
 * @param service Service to stop
 * @return True if service was stopped
 */
bool serviceStop(SERVICE *service);

/**
 * @brief Restart a stopped service
 *
 * @param service Service to restart
 * @return True if service was restarted
 */
bool serviceStart(SERVICE *service);

/**
 * @brief Start new a listener for a service
 *
 * @param service Service where the listener is linked
 * @param port Listener to start
 * @return True if listener was started
 */
bool serviceLaunchListener(SERVICE *service, SERV_LISTENER *port);

/**
 * @brief Stop a listener for a service
 *
 * @param service Service where the listener is linked
 * @param name Name of the listener
 * @return True if listener was stopped
 */
bool serviceStopListener(SERVICE *service, const char *name);

/**
 * @brief Restart a stopped listener
 *
 * @param service Service where the listener is linked
 * @param name Name of the listener
 * @return True if listener was restarted
 */
bool serviceStartListener(SERVICE *service, const char *name);

/**
 * Utility functions
 */
SERVICE* service_find(const char *name);

// TODO: Change binlogrouter to use the functions in config_runtime.h
bool serviceAddBackend(SERVICE *service, SERVER *server);

/**
 * @brief Check if a service uses a server
 * @param service Service to check
 * @param server Server being used
 * @return True if service uses the server
 */
bool serviceHasBackend(SERVICE *service, SERVER *server);

/**
 * @brief Check if a service has a listener
 *
 * @param service Service to check
 * @param protocol Listener protocol
 * @param address Listener address
 * @param port Listener port
 * @return True if service has the listener
 */
bool serviceHasListener(SERVICE *service, const char *protocol,
                        const char* address, unsigned short port);

int   serviceGetUser(SERVICE *service, char **user, char **auth);
int   serviceSetUser(SERVICE *service, char *user, char *auth);
bool  serviceSetFilters(SERVICE *service, char *filters);
int   serviceEnableRootUser(SERVICE *service, int action);
int   serviceSetTimeout(SERVICE *service, int val);
int   serviceSetConnectionLimits(SERVICE *service, int max, int queued, int timeout);
void  serviceSetRetryOnFailure(SERVICE *service, char* value);
void  serviceWeightBy(SERVICE *service, char *weightby);
char* serviceGetWeightingParameter(SERVICE *service);
int   serviceEnableLocalhostMatchWildcardHost(SERVICE *service, int action);
int   serviceStripDbEsc(SERVICE* service, int action);
int   serviceAuthAllServers(SERVICE *service, int action);
int   service_refresh_users(SERVICE *service);

/**
 * Diagnostics
 */
void       dprintAllServices(DCB *dcb);
void       dprintService(DCB *dcb, SERVICE *service);
void       dListServices(DCB *dcb);
void       dListListeners(DCB *dcb);
int        serviceSessionCountAll(void);
RESULTSET* serviceGetList(void);
RESULTSET* serviceGetListenerList(void);

/**
 * Get the capabilities of the servive.
 *
 * The capabilities of a service are the union of the capabilities of
 * its router and all filters.
 *
 * @return The service capabilities.
 */
static inline uint64_t service_get_capabilities(const SERVICE *service)
{
    return service->capabilities;
}

MXS_END_DECLS
