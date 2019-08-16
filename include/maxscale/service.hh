/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <cmath>
#include <ctime>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

#include <maxbase/jansson.h>
#include <maxscale/config.hh>
#include <maxscale/protocol.hh>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/filter.hh>
#include <maxscale/server.hh>
#include <maxscale/target.hh>

struct SERVER;
struct mxs_router;
struct mxs_router_object;
struct users;

#define MAX_SERVICE_USER_LEN     1024
#define MAX_SERVICE_PASSWORD_LEN 1024
#define MAX_SERVICE_WEIGHTBY_LEN 1024
#define MAX_SERVICE_VERSION_LEN  1024

typedef struct server_ref_t
{
    struct server_ref_t* next;          /**< Next server reference */
    SERVER*              server;        /**< The actual server */
    double               server_weight; /**< Weight in the range [0..1]. 0 is worst, and a special case. */
    int                  connections;   /**< Number of connections created through this reference */
    bool                 active;        /**< Whether this reference is valid and in use*/
} SERVER_REF;

inline bool server_ref_is_active(const SERVER_REF* ref)
{
    return ref->active && ref->server->server_is_active();
}

/** Value of service timeout if timeout checks are disabled */
#define SERVICE_NO_SESSION_TIMEOUT 0

/**
 * Parameters that are automatically detected but can also be configured by the
 * user are initially set to this value.
 */
#define SERVICE_PARAM_UNINIT -1

/* Refresh rate limits for load users from database */
#define USERS_REFRESH_TIME_DEFAULT 30   /* Allowed time interval (in seconds) after last update*/

/** Default timeout values used by the connections which fetch user authentication data */
#define DEFAULT_AUTH_CONNECT_TIMEOUT 3
#define DEFAULT_AUTH_READ_TIMEOUT    1
#define DEFAULT_AUTH_WRITE_TIMEOUT   2

enum service_version_which_t
{
    SERVICE_VERSION_ANY,    /*< Any version of the servers of a service. */
    SERVICE_VERSION_MIN,    /*< The minimum version. */
    SERVICE_VERSION_MAX,    /*< The maximum version. */
};

/**
 * Defines a service within the gateway.
 *
 * A service is a combination of a set of backend servers, a routing mechanism
 * and a set of client side protocol/port pairs used to listen for new connections
 * to the service.
 */
class SERVICE : public mxs::Target
{
public:

    enum class State
    {
        ALLOC,      /**< The service has been allocated */
        STARTED,    /**< The service has been started */
        FAILED,     /**< The service failed to start */
        STOPPED,    /**< The service has been stopped */
    };

    struct Config
    {
        Config(MXS_CONFIG_PARAMETER* params);

        std::string user;                           /**< Username */
        std::string password;                       /**< Password */
        std::string weightby;                       /**< Weighting parameter name */
        std::string version_string;                 /**< Version string sent to clients */
        int         max_connections;                /**< Maximum client connections */
        int         max_retry_interval;             /**< Maximum retry interval */
        bool        enable_root;                    /**< Allow root user  access */
        bool        localhost_match_wildcard_host;  /**< Match localhost against wildcard */
        bool        users_from_all;                 /**< Load users from all servers */
        bool        log_auth_warnings;              /**< Log authentication failures and warnings */
        bool        session_track_trx_state;        /**< Get transaction state via session track mechanism */
        int64_t     conn_idle_timeout;              /**< Session timeout in seconds */
        int64_t     net_write_timeout;              /**< Write timeout in seconds */
        int32_t     retain_last_statements;         /**< How many statements to retain per session,
                                                     * -1 if not explicitly specified. */

        bool strip_db_esc;      /**< Remove the '\' characters from database names when querying them from
                                 * the server. MySQL Workbench seems to escape at least the underscore
                                 * character. */
    };

    State              state {State::ALLOC};        /**< The service state */
    int                client_count {0};            /**< Number of connected clients */
    mxs_router_object* router {nullptr};            /**< The router we are using */
    mxs_router*        router_instance {nullptr};   /**< The router instance for this service */
    SERVER_REF*        dbref {nullptr};             /**< server references */
    time_t             started {0};                 /**< The time when the service was started */
    uint64_t           capabilities {0};            /**< The capabilities of the service,
                                                     * @see enum routing_capability */

    const char* name() const override
    {
        return m_name.c_str();
    }

    virtual const MXS_CONFIG_PARAMETER& params() const = 0;

    uint64_t status() const override
    {
        // TODO: Get this from the backend servers
        return SERVER_RUNNING | SERVER_MASTER;
    }

    bool active() const override
    {
        return m_active;
    }

    void deactivate()
    {
        m_active = false;
    }

    int64_t rank() const override
    {
        return RANK_PRIMARY;
    }

    virtual int64_t replication_lag() const = 0;

    const char* router_name() const
    {
        return m_router_name.c_str();
    }

    virtual const Config& config() const = 0;

    /**
     * Get server version
     *
     * @param which Which value to retrieve, the minimum, maximum or any value
     *
     * @return The corresponding backend server version
     */
    virtual uint64_t get_version(service_version_which_t which) const = 0;

    /**
     * Get all servers that are reachable from this service
     *
     * @return All servers that can be reached via this service
     */
    virtual std::vector<SERVER*> reachable_servers() const = 0;

protected:
    SERVICE(const std::string& name,
            const std::string& router_name)
        : started(time(nullptr))
        , m_name(name)
        , m_router_name(router_name)
    {
    }

private:
    const std::string m_name;
    const std::string m_router_name;
    bool              m_active {true};
};

typedef enum count_spec_t
{
    COUNT_NONE = 0,
    COUNT_ATLEAST,
    COUNT_EXACT,
    COUNT_ATMOST
} count_spec_t;

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
bool service_refresh_users(SERVICE* service);

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
