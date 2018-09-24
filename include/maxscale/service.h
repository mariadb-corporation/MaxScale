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

/**
 * @file service.h
 *
 * The service level definitions within the gateway
 */

#include <maxscale/cdefs.h>

#include <math.h>
#include <time.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

#include <maxbase/jansson.h>
#include <maxscale/protocol.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/server.h>
#include <maxscale/listener.h>
#include <maxscale/filter.h>
#include <maxscale/config.h>

MXS_BEGIN_DECLS

struct server;
struct mxs_router;
struct mxs_router_object;
struct users;

#define MAX_SERVICE_USER_LEN     1024
#define MAX_SERVICE_PASSWORD_LEN 1024
#define MAX_SERVICE_WEIGHTBY_LEN 1024
#define MAX_SERVICE_VERSION_LEN  1024

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

typedef struct server_ref_t
{
    struct server_ref_t* next;          /**< Next server reference */
    SERVER*              server;        /**< The actual server */
    double               server_weight; /**< Weight in the range [0..1]. 0 is worst, and a special case. */
    int                  connections;   /**< Number of connections created through this reference */
    bool                 active;        /**< Whether this reference is valid and in use*/
} SERVER_REF;

/** Macro to check whether a SERVER_REF is active */
#define SERVER_REF_IS_ACTIVE(ref) (ref->active && server_is_active(ref->server))

#define SERVICE_MAX_RETRY_INTERVAL 3600     /*< The maximum interval between service start retries */

/** Value of service timeout if timeout checks are disabled */
#define SERVICE_NO_SESSION_TIMEOUT 0

/**
 * Parameters that are automatically detected but can also be configured by the
 * user are initially set to this value.
 */
#define SERVICE_PARAM_UNINIT -1

/* Refresh rate limits for load users from database */
#define USERS_REFRESH_TIME_DEFAULT 30   /* Allowed time interval (in seconds) after last update*/
#define USERS_REFRESH_TIME_MIN     10   /* Minimum allowed time interval (in seconds)*/

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
    const char*    name;                            /**< The service name */
    int            state;                           /**< The service state */
    int            client_count;                    /**< Number of connected clients */
    int            max_connections;                 /**< Maximum client connections */
    SERV_LISTENER* ports;                           /**< Linked list of ports and
                                                     * protocols
                                                     * that this service will listen on */
    const char*               routerModule;         /**< Name of router module to use */
    struct mxs_router_object* router;               /**< The router we are using */
    struct mxs_router*        router_instance;      /**< The router instance for this
                                                     * service */
    char version_string[MAX_SERVICE_VERSION_LEN];   /**< version string for this service
                                                     * listeners */
    SERVER_REF* dbref;                              /**< server references */
    int         n_dbref;                            /**< Number of server references */
    char        user[MAX_SERVICE_USER_LEN];         /**< The user name to use to extract
                                                     * information */
    char password[MAX_SERVICE_PASSWORD_LEN];        /**< The authentication data requied
                                                     * */
    SERVICE_STATS stats;                            /**< The service statistics */
    bool          enable_root;                      /**< Allow root user  access */
    bool          localhost_match_wildcard_host;    /**< Match localhost against wildcard
                                                     * */
    MXS_CONFIG_PARAMETER* svc_config_param;         /**<  list of config params and values
                                                     * */
    int svc_config_version;                         /**<  Version number of configuration
                                                     * */
    bool svc_do_shutdown;                           /**< tells the service to exit loops
                                                     * etc. */
    bool users_from_all;                            /**< Load users from one server or all
                                                     * of them */
    bool strip_db_esc;                              /**< Remove the '\' characters from
                                                     * database names
                                                     * when querying them from the server.
                                                     * MySQL Workbench seems
                                                     * to escape at least the underscore
                                                     * character. */
    int64_t conn_idle_timeout;                      /**< Session timeout in seconds */
    char    weightby[MAX_SERVICE_WEIGHTBY_LEN];     /**< Service weighting parameter name
                                                     * */
    bool retry_start;                               /**< If starting of the service should
                                                     * be retried later */
    bool log_auth_warnings;                         /**< Log authentication failures and
                                                     * warnings */
    uint64_t capabilities;                          /**< The capabilities of the service,
                                                     * @see enum routing_capability */
    int  max_retry_interval;                        /**< Maximum retry interval */
    bool session_track_trx_state;                   /**< Get transaction state via session
                                                     * track mechanism */
    int active;                                     /**< Whether the service is still
                                                     * active */
} SERVICE;

typedef enum count_spec_t
{
    COUNT_NONE = 0,
    COUNT_ATLEAST,
    COUNT_EXACT,
    COUNT_ATMOST
} count_spec_t;

#define SERVICE_STATE_ALLOC   1         /**< The service has been allocated */
#define SERVICE_STATE_STARTED 2         /**< The service has been started */
#define SERVICE_STATE_FAILED  3         /**< The service failed to start */
#define SERVICE_STATE_STOPPED 4         /**< The service has been stopped */

/**
 * Find a service
 *
 * @param name Service name
 *
 * @return Service or NULL of no service was found
 */
SERVICE* service_find(const char* name);

/**
 * @brief Stop a service
 *
 * @param service Service to stop
 *
 * @return True if service was stopped
 */
bool serviceStop(SERVICE* service);

/**
 * @brief Restart a stopped service
 *
 * @param service Service to restart
 *
 * @return True if service was restarted
 */
bool serviceStart(SERVICE* service);

/**
 * @brief Stop a listener for a service
 *
 * @param service Service where the listener is linked
 * @param name Name of the listener
 *
 * @return True if listener was stopped
 */
bool serviceStopListener(SERVICE* service, const char* name);

/**
 * @brief Restart a stopped listener
 *
 * @param service Service where the listener is linked
 * @param name Name of the listener
 *
 * @return True if listener was restarted
 */
bool serviceStartListener(SERVICE* service, const char* name);

// TODO: Change binlogrouter to use the functions in config_runtime.h
bool serviceAddBackend(SERVICE* service, SERVER* server);

// Used by authenticators
void serviceGetUser(SERVICE* service, const char** user, const char** auth);

// Used by routers
const char* serviceGetWeightingParameter(SERVICE* service);

// Reload users
int service_refresh_users(SERVICE* service);

/**
 * Diagnostics
 */

/**
 * @brief Print service authenticator diagnostics
 *
 * @param dcb     DCB to print to
 * @param service The service to diagnose
 */
void service_print_users(DCB*, const SERVICE*);

void dprintAllServices(DCB* dcb);
void dprintService(DCB* dcb, SERVICE* service);
void dListServices(DCB* dcb);
void dListListeners(DCB* dcb);
int  serviceSessionCountAll(void);

/**
 * Get the capabilities of the servive.
 *
 * The capabilities of a service are the union of the capabilities of
 * its router and all filters.
 *
 * @return The service capabilities.
 */
static inline uint64_t service_get_capabilities(const SERVICE* service)
{
    return service->capabilities;
}

typedef enum service_version_which_t
{
    SERVICE_VERSION_ANY,    /*< Any version of the servers of a service. */
    SERVICE_VERSION_MIN,    /*< The minimum version. */
    SERVICE_VERSION_MAX,    /*< The maximum version. */
} service_version_which_t;

/**
 * Return the version of the service. The returned version can be
 *
 * - the version of any (in practice the first) server associated
 *   with the service,
 * - the smallest version of any of the servers associated with
 *   the service, or
 * - the largest version of any of the servers associated with
 *   the service.
 *
 * @param service  The service.
 * @param which    Which version.
 *
 * @return The version of the service.
 */
uint64_t service_get_version(const SERVICE* service, service_version_which_t which);

MXS_END_DECLS
