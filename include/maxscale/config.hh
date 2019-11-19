/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>

/**
 * The gateway global configuration data
 */
class MXS_CONFIG : public config::Configuration
{
public:
    MXS_CONFIG();

    MXS_CONFIG(const MXS_CONFIG&) = delete;
    MXS_CONFIG& operator=(const MXS_CONFIG&) = delete;

    bool    config_check;                               /**< Only check config */
    int     n_threads;                                  /**< Number of polling threads */
    size_t  thread_stack_size;                          /**< The stack size of each worker thread */
    char    release_string[RELEASE_STR_LENGTH];         /**< The release name string of the system */
    char    sysname[SYSNAME_LEN];                       /**< The OS name of the system */
    uint8_t mac_sha1[SHA_DIGEST_LENGTH];                /**< The SHA1 digest of an interface MAC address */

    int    syslog;                                      /**< Log to syslog */
    int    maxlog;                                      /**< Log to MaxScale's own logs */
    time_t auth_conn_timeout;                           /**< Connection timeout for the user
                                                         * authentication */
    time_t  auth_read_timeout;                          /**< Read timeout for the user authentication */
    time_t  auth_write_timeout;                         /**< Write timeout for the user authentication */
    bool    skip_permission_checks;                     /**< Skip service and monitor permission checks */
    int32_t passive;                                    /**< True if MaxScale is in passive mode */
    int64_t promoted_at;                                /**< Time when this Maxscale instance was
                                                        * promoted from a passive to an active */
    char                qc_name[PATH_MAX];              /**< The name of the query classifier to load */
    char*               qc_args;                        /**< Arguments for the query classifier */
    QC_CACHE_PROPERTIES qc_cache_properties;            /**< The query classifier cache properties. */
    qc_sql_mode_t       qc_sql_mode;                    /**< The query classifier sql mode */
    char                admin_host[MAX_ADMIN_HOST_LEN]; /**< Admin interface host */
    uint16_t            admin_port;                     /**< Admin interface port */
    bool                admin_auth;                     /**< Admin interface authentication */
    bool                admin_enabled;                  /**< Admin interface is enabled */
    bool                admin_log_auth_failures;        /**< Log admin interface authentication failures */
    std::string         admin_pam_rw_service;           /**< PAM service for read-write users */
    std::string         admin_pam_ro_service;           /**< PAM service for read-only users */

    char admin_ssl_key[PATH_MAX];                       /**< Admin SSL key */
    char admin_ssl_cert[PATH_MAX];                      /**< Admin SSL cert */
    char admin_ssl_ca_cert[PATH_MAX];                   /**< Admin SSL CA cert */
    int  query_retries;                                 /**< Number of times a interrupted query is
                                                         * retried */
    time_t query_retry_timeout;                         /**< Timeout for query retries */
    bool   substitute_variables;                        /**< Should environment variables be substituted
                                                         * */
    char*    local_address;                             /**< Local address to use when connecting */
    time_t   users_refresh_time;                        /**< How often the users can be refreshed */
    time_t   users_refresh_interval;                    /**< How often the users will be refreshed */
    uint64_t writeq_high_water;                         /**< High water mark of dcb write queue */
    uint64_t writeq_low_water;                          /**< Low water mark of dcb write queue */
    char     peer_hosts[MAX_ADMIN_HOST_LEN];            /**< The protocol, address and port for peers
                                                         * (currently only one) */
    char             peer_user[MAX_ADMIN_HOST_LEN];     /**< Username for maxscale-to-maxscale traffic */
    char             peer_password[MAX_ADMIN_HOST_LEN]; /**< Password for maxscale-to-maxscale traffic */
    mxb_log_target_t log_target;                        /**< Log type */
    bool             load_persisted_configs;            /**< Load persisted configuration files on startup */
    int              max_auth_errors_until_block;       /**< Host is blocked once this limit is reached */
    int rebalance_threshold;                            /**< If load of particular worker differs more than
                                                         * this % amount from load-average, rebalancing will be made
                                                         */

private:
    static config::Specification s_specification;
};

/**
 * @brief Get global MaxScale configuration
 *
 * @return The global configuration
 */
MXS_CONFIG* config_get_global_options();
