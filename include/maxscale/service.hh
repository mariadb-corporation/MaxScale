/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
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
#include <maxscale/filter.hh>
#include <maxscale/server.hh>
#include <maxscale/target.hh>
#include <maxscale/workerlocal.hh>
#include <maxscale/router.hh>
#include <maxscale/monitor.hh>

class SERVER;
struct users;

#define MAX_SERVICE_USER_LEN     1024
#define MAX_SERVICE_PASSWORD_LEN 1024
#define MAX_SERVICE_VERSION_LEN  1024

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
#define DEFAULT_AUTH_CONNECT_TIMEOUT 10
#define DEFAULT_AUTH_READ_TIMEOUT    10
#define DEFAULT_AUTH_WRITE_TIMEOUT   10

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
        Config(mxs::ConfigParameters* params);

        std::string user;                           /**< Username */
        std::string password;                       /**< Password */
        std::string version_string;                 /**< Version string sent to clients */
        int         max_connections;                /**< Maximum client connections */
        bool        enable_root;                    /**< Allow root user  access */
        bool        users_from_all;                 /**< Load users from all servers */
        bool        log_auth_warnings;              /**< Log authentication failures and warnings */
        bool        session_track_trx_state;        /**< Get transaction state via session track mechanism */
        int64_t     conn_idle_timeout;              /**< Session timeout in seconds */
        int64_t     net_write_timeout;              /**< Write timeout in seconds */
        int32_t     retain_last_statements;         /**< How many statements to retain per session,
                                                     * -1 if not explicitly specified. */
        int64_t connection_keepalive;               /**< How often to ping idle sessions */

        /**
         * Remove the '\' characters from database names when querying them from the server. This
         * is required when users make grants such as "grant select on `test\_1`.* to ..." to avoid
         * wildcard matching against _. A plain "grant select on `test_1`.* to ..." would normally
         * grant access to e.g. testA1. MaxScale does not support this type of wilcard matching for
         * the database, but it must still understand the escaped version of the grant. */
        bool strip_db_esc {true};

        int64_t rank;   /*< The ranking of this service */

        bool    prune_sescmd_history {false};
        bool    disable_sescmd_history {false};
        int64_t max_sescmd_history {50};

        /**
         * Can backend connections be pooled while session is still running? Configured as seconds the
         * session must be idle before backend connections can be pooled. */
        int64_t idle_session_pooling_time {-1};
    };

    State  state {State::ALLOC};        /**< The service state */
    time_t started {0};                 /**< The time when the service was started */

    const char* name() const override
    {
        return m_name.c_str();
    }

    virtual const mxs::ConfigParameters& params() const = 0;

    const char* router_name() const
    {
        return m_router_name.c_str();
    }

    /**
     * Get service configuration
     *
     * The returned configuration can only be accessed on a RoutingWorker thread.
     *
     * @return Reference to the WorkerGlobal configuration object
     */
    virtual const mxs::WorkerGlobal<Config>& config() const = 0;

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

    /**
     * Get the cluster that the service uses
     *
     * @return The cluster that the service uses or nullptr if it doesn't use one
     */
    virtual mxs::Monitor* cluster() const = 0;

    /**
     * Get the user account cache for the current routing worker. Should be only called from a routing
     * worker.
     *
     * @return Thread-specific user account cache
     */
    virtual const mxs::UserAccountCache* user_account_cache() const = 0;

    /**
     * Notify the service that authentication failed. The service may forward the notification to its user
     * account manager which then updates its data.
     */
    virtual void request_user_account_update() = 0;

    /**
     *  The user account manager should call this function after it has read user data from a backend
     *  and updated its internal database. Calling this function causes the service to sync all
     *  thread-specific user data caches with the master data.
     *
     *  Even empty (no changes) and failed updates should be broadcasted as they may be of interest
     *  to any sessions waiting for user account data.
     */
    virtual void sync_user_account_caches() = 0;

    /**
     * Add a client connection to the list of clients to wakeup on userdata load.
     *
     * @param client Client connection to add
     */
    virtual void mark_for_wakeup(mxs::ClientConnection* client) = 0;

    /**
     * Remove a client connection from the wakeup list. Typically only needed when a sleeping connection
     * is closed.
     *
     * @param client Client connection to remove
     */
    virtual void unmark_for_wakeup(mxs::ClientConnection* client) = 0;

    /**
     * Has a connection limit been reached?
     */
    bool has_too_many_connections() const
    {
        auto limit = config()->max_connections;
        return limit && mxb::atomic::load(&stats().n_current, mxb::atomic::RELAXED) >= limit;
    }

    /**
     * Get the version string of the service. If a version string is configured, returns that. Otherwise
     * returns the version string of the server with the smallest version number.
     *
     * @return Version string
     */
    std::string version_string() const;

    /**
     * Get custom version suffix. Used by client protocol when generating server handshake.
     *
     * @return Version suffix
     */
    const std::string& custom_version_suffix();

    /**
     * Set custom version suffix. This is meant to be used by a router which wants to add custom text to
     * any version string sent to clients. Should be only called during service/router creation,
     * as there is no concurrency protection.
     *
     * @param custom_version_suffix The version suffix to set
     */
    void set_custom_version_suffix(const std::string& custom_version_suffix);

    uint8_t charset() const;

    mxs::Router* router() const;

protected:
    SERVICE(const std::string& name,
            const std::string& router_name)
        : started(time(nullptr))
        , m_name(name)
        , m_router_name(router_name)
    {
    }

    uint64_t     m_capabilities {0};/**< The capabilities of the service, @see enum routing_capability */
    mxs::Router* m_router {nullptr};/**< The router instance for this service */

private:
    const std::string m_name;
    const std::string m_router_name;
    std::string       m_custom_version_suffix;
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

// Used by authenticators
void serviceGetUser(SERVICE* service, const char** user, const char** auth);

/**
 * Diagnostics
 */

int serviceSessionCountAll(void);

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
    return service->capabilities();
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
