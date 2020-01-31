/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-01-15
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
    MXS_CONFIG(const MXS_CONFIG&) = delete;
    MXS_CONFIG& operator=(const MXS_CONFIG&) = delete;

    class RebalancePeriod : public config::Milliseconds
    {
    public:
        using config::Milliseconds::Milliseconds;

    protected:
        void do_set(const value_type& value) override final;
    };

    class ParamUsersRefreshTime : public config::ParamSeconds
    {
    public:
        using config::ParamSeconds::ParamSeconds;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const;
    };

    class QcCacheMaxSize : public config::Size
    {
    public:
        using config::Size::Size;

    protected:
        void do_set(const value_type& value) override final;
    };

    class Passive : public config::Bool
    {
    public:
        using config::Bool::Bool;

        void do_set(const value_type& value) override final;
    };

    MXS_CONFIG();

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
    config::Bool    skip_permission_checks;             /**< Skip service and monitor permission checks */
    Passive         passive;                            /**< True if MaxScale is in passive mode */
    config::String  qc_name;                            /**< The name of the query classifier to load */
    config::String  qc_args;                            /**< Arguments for the query classifieer */
    QcCacheMaxSize  qc_cache_max_size;                  /**< Maximum amount of memory used by qc */
    config::Enum<qc_sql_mode_t> qc_sql_mode;            /**< The query classifier sql mode */
    config::String  admin_host;                         /**< Admin interface host */
    config::Integer admin_port;                         /**< Admin interface port */
    config::Bool    admin_auth;                         /**< Admin interface authentication */
    config::Bool    admin_enabled;                      /**< Admin interface is enabled */
    config::Bool    admin_log_auth_failures;            /**< Log admin interface authentication failures */
    config::String  admin_pam_rw_service;               /**< PAM service for read-write users */
    config::String  admin_pam_ro_service;               /**< PAM service for read-only users */
    config::String  admin_ssl_key;                      /**< Admin SSL key */
    config::String  admin_ssl_cert;                     /**< Admin SSL cert */
    config::String  admin_ssl_ca_cert;                  /**< Admin SSL CA cert */
    config::Integer query_retries;                      /**< Number of times a interrupted query is
                                                         * retried */
    config::Seconds query_retry_timeout;                /**< Timeout for query retries */
    config::String  local_address;                      /**< Local address to use when connecting */
    config::Seconds users_refresh_time;                 /**< How often the users can be refreshed */
    config::Seconds users_refresh_interval;             /**< How often the users will be refreshed */
    config::Size    writeq_high_water;                  /**< High water mark of dcb write queue */
    config::Size    writeq_low_water;                   /**< Low water mark of dcb write queue */
    config::Bool    load_persisted_configs;             /**< Load persisted configuration files on startup */
    config::Integer max_auth_errors_until_block;        /**< Host is blocked once this limit is reached */
    config::Integer rebalance_threshold;                /**< If load of particular worker differs more than
                                                         * this % amount from load-average, rebalancing will
                                                         * be made.
                                                         */
    RebalancePeriod rebalance_period;                   /**< How often should rebalancing be made. */
    config::Count   rebalance_window;                   /**< How many seconds should be taken into account. */

    // The following will not be configured via the configuration mechanism.
    mxb_log_target_t    log_target;                     /**< Log type */
    bool                substitute_variables;           /**< Should environment variables be substituted */
    QC_CACHE_PROPERTIES qc_cache_properties;            /**< The query classifier cache properties. */
    int64_t             promoted_at;                    /**< Time when this Maxscale instance was
                                                         * promoted from a passive to an active */

    bool post_configure(const mxs::ConfigParameters& params) override;

public:
    static config::Specification s_specification;

    static config::ParamBool                s_skip_permission_checks;
    static config::ParamBool                s_passive;
    static config::ParamString              s_qc_name;
    static config::ParamString              s_qc_args;
    static config::ParamSize                s_qc_cache_max_size;
    static config::ParamEnum<qc_sql_mode_t> s_qc_sql_mode;
    static config::ParamString              s_admin_host;
    static config::ParamInteger             s_admin_port;
    static config::ParamBool                s_admin_auth;
    static config::ParamBool                s_admin_enabled;
    static config::ParamBool                s_admin_log_auth_failures;
    static config::ParamString              s_admin_pam_rw_service;
    static config::ParamString              s_admin_pam_ro_service;
    static config::ParamString              s_admin_ssl_key;
    static config::ParamString              s_admin_ssl_cert;
    static config::ParamString              s_admin_ssl_ca_cert;
    static config::ParamInteger             s_query_retries;
    static config::ParamSeconds             s_query_retry_timeout;
    static config::ParamString              s_local_address;
    static ParamUsersRefreshTime            s_users_refresh_time;
    static config::ParamSeconds             s_users_refresh_interval;
    static config::ParamSize                s_writeq_high_water;
    static config::ParamSize                s_writeq_low_water;
    static config::ParamBool                s_load_persisted_configs;
    static config::ParamInteger             s_max_auth_errors_until_block;
    static config::ParamInteger             s_rebalance_threshold;
    static config::ParamMilliseconds        s_rebalance_period;
    static config::ParamCount               s_rebalance_window;
};

/**
 * @brief Get global MaxScale configuration
 *
 * @return The global configuration
 */
MXS_CONFIG* config_get_global_options();
