/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
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
class MXS_CONFIG : public mxs::cfg::Configuration
{
public:
    MXS_CONFIG(const MXS_CONFIG&) = delete;
    MXS_CONFIG& operator=(const MXS_CONFIG&) = delete;

    class ParamUsersRefreshTime : public mxs::cfg::ParamSeconds
    {
    public:
        using mxs::cfg::ParamSeconds::ParamSeconds;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const;
    };

    class ParamLogThrottling : public mxs::cfg::Param
    {
    public:
        using value_type = MXS_LOG_THROTTLING;

        ParamLogThrottling(mxs::cfg::Specification* pSpecification,
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

    class LogThrottling : public mxs::cfg::ConcreteType<ParamLogThrottling>
    {
    public:
        LogThrottling(Configuration* pConfiguration,
                      const ParamLogThrottling* pParam,
                      std::function<void (value_type)> on_set = nullptr)
            : mxs::cfg::ConcreteType<ParamLogThrottling>(pConfiguration, pParam, on_set)
        {
        }
    };

    class ParamThreadsCount : public mxs::cfg::ParamCount
    {
    public:
        using mxs::cfg::ParamCount::ParamCount;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const;
    };

    using SessionDumpStatements = mxs::cfg::Enum<session_dump_statements_t>;

    MXS_CONFIG();

    bool    config_check;                               /**< Only check config */
    char    release_string[RELEASE_STR_LENGTH];         /**< The release name string of the system */
    char    sysname[SYSNAME_LEN];                       /**< The OS name of the system */
    uint8_t mac_sha1[SHA_DIGEST_LENGTH];                /**< The SHA1 digest of an interface MAC address */

    mxs::cfg::Bool         log_debug;                   /**< Whether debug messages are logged. */
    mxs::cfg::Bool         log_info;                    /**< Whether info messages are logged. */
    mxs::cfg::Bool         log_notice;                  /**< Whether notice messages are logged. */
    mxs::cfg::Bool         log_warning;                 /**< Whether warning messages are logged. */
    LogThrottling          log_throttling;              /**< When and how to throttle logged messaged. */
    int64_t                n_threads;                   /**< Number of polling threads */
    SessionDumpStatements  dump_statements;             /**< Whether to dump last statements. */
    mxs::cfg::Count        session_trace;               /**< How entries stored to session trace log.*/
    mxs::cfg::Bool         ms_timestamp;                /**< Enable or disable high precision timestamps */
    mxs::cfg::Count        retain_last_statements;      /**< How many statements should be retained. */
    mxs::cfg::Bool         syslog;                      /**< Log to syslog */
    mxs::cfg::Bool         maxlog;                      /**< Log to MaxScale's own logs */
    mxs::cfg::Seconds      auth_conn_timeout;           /**< Connection timeout for the user authentication */
    mxs::cfg::Seconds      auth_read_timeout;           /**< Read timeout for the user authentication */
    mxs::cfg::Seconds      auth_write_timeout;          /**< Write timeout for the user authentication */
    mxs::cfg::Bool         skip_permission_checks;      /**< Skip service and monitor permission checks */
    mxs::cfg::Bool         passive;                     /**< True if MaxScale is in passive mode */
    std::string            qc_name;                     /**< The name of the query classifier to load */
    std::string            qc_args;                     /**< Arguments for the query classifieer */
    mxs::cfg::Size         qc_cache_max_size;           /**< Maximum amount of memory used by qc */
    qc_sql_mode_t          qc_sql_mode;                 /**< The query classifier sql mode */
    std::string            admin_host;                  /**< Admin interface host */
    int64_t                admin_port;                  /**< Admin interface port */
    bool                   admin_auth;                  /**< Admin interface authentication */
    bool                   admin_enabled;               /**< Admin interface is enabled */
    mxs::cfg::Bool         admin_log_auth_failures;     /**< Log admin interface authentication failures */
    std::string            admin_pam_rw_service;        /**< PAM service for read-write users */
    std::string            admin_pam_ro_service;        /**< PAM service for read-only users */
    std::string            admin_ssl_key;               /**< Admin SSL key */
    std::string            admin_ssl_cert;              /**< Admin SSL cert */
    std::string            admin_ssl_ca_cert;           /**< Admin SSL CA cert */
    mxs::cfg::Integer      query_retries;               /**< Number of times a interrupted query is
                                                         * retried */
    mxs::cfg::Seconds      query_retry_timeout;         /**< Timeout for query retries */
    std::string            local_address;               /**< Local address to use when connecting */
    mxs::cfg::Seconds      users_refresh_time;          /**< How often the users can be refreshed */
    mxs::cfg::Seconds      users_refresh_interval;      /**< How often the users will be refreshed */
    mxs::cfg::Size         writeq_high_water;           /**< High water mark of dcb write queue */
    mxs::cfg::Size         writeq_low_water;            /**< Low water mark of dcb write queue */
    bool                   load_persisted_configs;      /**< Load persisted configuration files on startup */
    mxs::cfg::Integer      max_auth_errors_until_block; /**< Host is blocked once this limit is reached */
    mxs::cfg::Integer      rebalance_threshold;         /**< If load of particular worker differs more than
                                                         * this % amount from load-average, rebalancing will
                                                         * be made.
                                                         */
    mxs::cfg::Milliseconds rebalance_period;            /**< How often should rebalancing be made. */
    mxs::cfg::Count        rebalance_window;            /**< How many seconds should be taken into account. */

    // The following will not be configured via the configuration mechanism.
    mxb_log_target_t       log_target;                  /**< Log type */
    bool                   substitute_variables;        /**< Should environment variables be substituted */
    QC_CACHE_PROPERTIES    qc_cache_properties;         /**< The query classifier cache properties. */
    int64_t                promoted_at;                 /**< Time when this Maxscale instance was
                                                         * promoted from a passive to an active */

    bool configure(const mxs::ConfigParameters& params,
                   mxs::ConfigParameters* pUnrecognized = nullptr) override;

private:
    bool post_configure(const mxs::ConfigParameters& params) override;

public:
    class Specification : public mxs::cfg::Specification
    {
    public:
        using mxs::cfg::Specification::Specification;

        bool validate(const mxs::ConfigParameters& params,
                      mxs::ConfigParameters* pUnrecognized = nullptr) const override final;
    };

    static Specification                                  s_specification;

    static mxs::cfg::ParamBool                            s_log_debug;
    static mxs::cfg::ParamBool                            s_log_info;
    static mxs::cfg::ParamBool                            s_log_notice;
    static mxs::cfg::ParamBool                            s_log_warning;
    static ParamLogThrottling                             s_log_throttling;
    static ParamThreadsCount                              s_n_threads;
    static mxs::cfg::ParamEnum<session_dump_statements_t> s_dump_statements;
    static mxs::cfg::ParamCount                           s_session_trace;
    static mxs::cfg::ParamBool                            s_ms_timestamp;
    static mxs::cfg::ParamCount                           s_retain_last_statements;
    static mxs::cfg::ParamBool                            s_syslog;
    static mxs::cfg::ParamBool                            s_maxlog;
    static mxs::cfg::ParamSeconds                         s_auth_conn_timeout;
    static mxs::cfg::ParamSeconds                         s_auth_read_timeout;
    static mxs::cfg::ParamSeconds                         s_auth_write_timeout;
    static mxs::cfg::ParamBool                            s_skip_permission_checks;
    static mxs::cfg::ParamBool                            s_passive;
    static mxs::cfg::ParamString                          s_qc_name;
    static mxs::cfg::ParamString                          s_qc_args;
    static mxs::cfg::ParamSize                            s_qc_cache_max_size;
    static mxs::cfg::ParamEnum<qc_sql_mode_t>             s_qc_sql_mode;
    static mxs::cfg::ParamString                          s_admin_host;
    static mxs::cfg::ParamInteger                         s_admin_port;
    static mxs::cfg::ParamBool                            s_admin_auth;
    static mxs::cfg::ParamBool                            s_admin_enabled;
    static mxs::cfg::ParamBool                            s_admin_log_auth_failures;
    static mxs::cfg::ParamString                          s_admin_pam_rw_service;
    static mxs::cfg::ParamString                          s_admin_pam_ro_service;
    static mxs::cfg::ParamString                          s_admin_ssl_key;
    static mxs::cfg::ParamString                          s_admin_ssl_cert;
    static mxs::cfg::ParamString                          s_admin_ssl_ca_cert;
    static mxs::cfg::ParamInteger                         s_query_retries;
    static mxs::cfg::ParamSeconds                         s_query_retry_timeout;
    static mxs::cfg::ParamString                          s_local_address;
    static ParamUsersRefreshTime                          s_users_refresh_time;
    static mxs::cfg::ParamSeconds                         s_users_refresh_interval;
    static mxs::cfg::ParamSize                            s_writeq_high_water;
    static mxs::cfg::ParamSize                            s_writeq_low_water;
    static mxs::cfg::ParamBool                            s_load_persisted_configs;
    static mxs::cfg::ParamInteger                         s_max_auth_errors_until_block;
    static mxs::cfg::ParamInteger                         s_rebalance_threshold;
    static mxs::cfg::ParamMilliseconds                    s_rebalance_period;
    static mxs::cfg::ParamCount                           s_rebalance_window;
};

/**
 * @brief Get global MaxScale configuration
 *
 * @return The global configuration
 */
MXS_CONFIG* config_get_global_options();
