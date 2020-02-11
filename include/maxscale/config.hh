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
#include <maxscale/session.hh>

/**
 * The gateway global configuration data
 */
class MXS_CONFIG : public config::Configuration
{
public:
    MXS_CONFIG(const MXS_CONFIG&) = delete;
    MXS_CONFIG& operator=(const MXS_CONFIG&) = delete;

    class ParamUsersRefreshTime : public config::ParamSeconds
    {
    public:
        using config::ParamSeconds::ParamSeconds;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const;
    };

    class ParamLogThrottling : public config::Param
    {
    public:
        using value_type = MXS_LOG_THROTTLING;

        ParamLogThrottling(config::Specification* pSpecification,
                           const char* zName,
                           const char* zDescription)
            : Param(pSpecification, zName, zDescription,
                    Modifiable::AT_RUNTIME,
                    Param::OPTIONAL,
                    MXS_MODULE_PARAM_STRING)
        {
        }

        std::string type() const override final;
        value_type default_value() const
        {
            return m_default_value;
        }
        std::string default_to_string() const override final;
        bool validate(const std::string& value_as_string, std::string* pMessage) const override final;

        std::string to_string(const value_type& value) const;
        bool from_string(const std::string& value, value_type* pValue, std::string* pMessage = nullptr) const;

        json_t* to_json(const value_type& value) const;
        bool from_json(const json_t* pJson, value_type* pValue, std::string* pMessage = nullptr) const;

        bool is_valid(const value_type&) const
        {
            return true;
        }

    private:
        const value_type m_default_value = { 0, 0, 0 };
    };

    class LogThrottling : public config::ConcreteType<LogThrottling, ParamLogThrottling>
    {
    public:
        LogThrottling(Configuration* pConfiguration,
                      const ParamLogThrottling* pParam,
                      std::function<void (value_type)> on_set = nullptr)
            : config::ConcreteType<LogThrottling, ParamLogThrottling>(pConfiguration, pParam, on_set)
        {
        }
    };

    class ParamThreadsCount : public config::ParamCount
    {
    public:
        using config::ParamCount::ParamCount;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const;
    };

    using SessionDumpStatements = config::Enum<session_dump_statements_t>;

    MXS_CONFIG();

    bool    config_check;                               /**< Only check config */
    char    release_string[RELEASE_STR_LENGTH];         /**< The release name string of the system */
    char    sysname[SYSNAME_LEN];                       /**< The OS name of the system */
    uint8_t mac_sha1[SHA_DIGEST_LENGTH];                /**< The SHA1 digest of an interface MAC address */

    config::Bool    log_debug;                          /**< Whether debug messages are logged. */
    config::Bool    log_info;                           /**< Whether info messages are logged. */
    config::Bool    log_notice;                         /**< Whether notice messages are logged. */
    config::Bool    log_warning;                        /**< Whether warning messages are logged. */
    LogThrottling   log_throttling;                     /**< When and how to throttle logged messaged. */
    config::Count   n_threads;                          /**< Number of polling threads */
    SessionDumpStatements dump_statements;              /**< Whether to dump last statements. */
    config::Count   session_trace;                      /**< How entries stored to session trace log.*/
    config::Bool    ms_timestamp;                       /**< Enable or disable high precision timestamps */
    config::Count   retain_last_statements;             /**< How many statements should be retained. */
    config::Bool    syslog;                             /**< Log to syslog */
    config::Bool    maxlog;                             /**< Log to MaxScale's own logs */
    config::Seconds auth_conn_timeout;                  /**< Connection timeout for the user authentication */
    config::Seconds auth_read_timeout;                  /**< Read timeout for the user authentication */
    config::Seconds auth_write_timeout;                 /**< Write timeout for the user authentication */
    config::Bool    skip_permission_checks;             /**< Skip service and monitor permission checks */
    config::Bool    passive;                            /**< True if MaxScale is in passive mode */
    config::String  qc_name;                            /**< The name of the query classifier to load */
    config::String  qc_args;                            /**< Arguments for the query classifieer */
    config::Size    qc_cache_max_size;                  /**< Maximum amount of memory used by qc */
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
    config::Milliseconds rebalance_period;              /**< How often should rebalancing be made. */
    config::Count   rebalance_window;                   /**< How many seconds should be taken into account. */

    // The following will not be configured via the configuration mechanism.
    mxb_log_target_t    log_target;                     /**< Log type */
    bool                substitute_variables;           /**< Should environment variables be substituted */
    QC_CACHE_PROPERTIES qc_cache_properties;            /**< The query classifier cache properties. */
    int64_t             promoted_at;                    /**< Time when this Maxscale instance was
                                                         * promoted from a passive to an active */

    bool configure(const mxs::ConfigParameters& params,
                   mxs::ConfigParameters* pUnrecognized = nullptr) override;

private:
    bool post_configure(const mxs::ConfigParameters& params) override;

public:
    class Specification : public config::Specification
    {
    public:
        using config::Specification::Specification;

        bool validate(const mxs::ConfigParameters& params,
                      mxs::ConfigParameters* pUnrecognized = nullptr) const override final;
    };

    static Specification                                s_specification;

    static config::ParamBool                            s_log_debug;
    static config::ParamBool                            s_log_info;
    static config::ParamBool                            s_log_notice;
    static config::ParamBool                            s_log_warning;
    static ParamLogThrottling                           s_log_throttling;
    static ParamThreadsCount                            s_n_threads;
    static config::ParamEnum<session_dump_statements_t> s_dump_statements;
    static config::ParamCount                           s_session_trace;
    static config::ParamBool                            s_ms_timestamp;
    static config::ParamCount                           s_retain_last_statements;
    static config::ParamBool                            s_syslog;
    static config::ParamBool                            s_maxlog;
    static config::ParamSeconds                         s_auth_conn_timeout;
    static config::ParamSeconds                         s_auth_read_timeout;
    static config::ParamSeconds                         s_auth_write_timeout;
    static config::ParamBool                            s_skip_permission_checks;
    static config::ParamBool                            s_passive;
    static config::ParamString                          s_qc_name;
    static config::ParamString                          s_qc_args;
    static config::ParamSize                            s_qc_cache_max_size;
    static config::ParamEnum<qc_sql_mode_t>             s_qc_sql_mode;
    static config::ParamString                          s_admin_host;
    static config::ParamInteger                         s_admin_port;
    static config::ParamBool                            s_admin_auth;
    static config::ParamBool                            s_admin_enabled;
    static config::ParamBool                            s_admin_log_auth_failures;
    static config::ParamString                          s_admin_pam_rw_service;
    static config::ParamString                          s_admin_pam_ro_service;
    static config::ParamString                          s_admin_ssl_key;
    static config::ParamString                          s_admin_ssl_cert;
    static config::ParamString                          s_admin_ssl_ca_cert;
    static config::ParamInteger                         s_query_retries;
    static config::ParamSeconds                         s_query_retry_timeout;
    static config::ParamString                          s_local_address;
    static ParamUsersRefreshTime                        s_users_refresh_time;
    static config::ParamSeconds                         s_users_refresh_interval;
    static config::ParamSize                            s_writeq_high_water;
    static config::ParamSize                            s_writeq_low_water;
    static config::ParamBool                            s_load_persisted_configs;
    static config::ParamInteger                         s_max_auth_errors_until_block;
    static config::ParamInteger                         s_rebalance_threshold;
    static config::ParamMilliseconds                    s_rebalance_period;
    static config::ParamCount                           s_rebalance_window;
};

/**
 * @brief Get global MaxScale configuration
 *
 * @return The global configuration
 */
MXS_CONFIG* config_get_global_options();
