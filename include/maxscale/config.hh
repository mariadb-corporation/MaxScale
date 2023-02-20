/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/http.hh>
#include <maxbase/ssl.hh>
#include <maxscale/config2.hh>
#include <maxscale/key_manager.hh>
#include <maxscale/cachingparser.hh>
#include <maxscale/session.hh>

namespace maxscale
{

// JSON Web Token signature algorithms
enum class JwtAlgo
{
    // Auto-selects a suitable algorithm. If public keys are used, uses them to generate the signatures. If
    // none are specified, uses a random symmetric key.
    AUTO,

    // HMAC with SHA-2
    // https://datatracker.ietf.org/doc/html/rfc7518#section-3.2
    HS256,
    HS384,
    HS512,

    // Digital Signature with RSASSA-PKCS1-v1_5
    // https://datatracker.ietf.org/doc/html/rfc7518#section-3.3
    RS256,
    RS384,
    RS512,

    // Digital Signature with ECDSA
    // https://datatracker.ietf.org/doc/html/rfc7518#section-3.4
    ES256,
    ES384,
    ES512,

    // Digital Signature with RSASSA-PSS
    // https://datatracker.ietf.org/doc/html/rfc7518#section-3.5
    PS256,
    PS384,
    PS512,

    // Edwards-curve Digital Signature Algorithm (EdDSA)
    // https://www.rfc-editor.org/rfc/rfc8037#section-3
    ED25519,
    ED448,
};

/**
 * The gateway global configuration data
 */
class Config : public config::Configuration
{
public:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    using seconds = std::chrono::seconds;
    using milliseconds = std::chrono::milliseconds;

    // If this is changed, update the documentation of `threads_max` as well.
    static const int64_t DEFAULT_THREADS_MAX = 256;

    /**
     * Initialize the config object. To be called *once* at program startup.
     *
     * @param argc  The argc provided to main.
     * @param argv  The argv provided to main.
     *
     * @return The MaxScale global configuration.
     */
    static Config& init(int argc, char** argv);

    /**
     * @return The MaxScale global configuration.
     */
    static Config& get();

    /**
     * Check if an object was read from a static configuration file
     *
     * @param name Name of the object
     *
     * @return True if an object with the given name was read from the configuration
     */
    static bool is_static_object(const std::string& name);

    /**
     * Check if an object was created at runtime or read from a persisted configuration file
     *
     * @param name Name of the object
     *
     * @return True if an object with the given name was created at runtime or read from a persisted
     *         configuration file
     */
    static bool is_dynamic_object(const std::string& name);

    /**
     * Set the file where the object is stored in
     *
     * @param name The object name
     * @param file The file where the object is stored in
     */
    static void set_object_source_file(const std::string& name, const std::string& file);

    /**
     * Get object source type and file as a JSON object
     *
     * @param name Name of the object
     *
     * @return A JSON object that contains the source type and the file the object was read from
     */
    static json_t* object_source_to_json(const std::string& name);

    /**
     * Persists global MaxScale options to a stream
     *
     * @param os The stream where the configuration is written to
     *
     * @return The same output stream
     */
    std::ostream& persist_maxscale(std::ostream& os) const;

    /**
     * Get global MaxScale configuration as JSON
     *
     * @param host Hostname of this server
     *
     * @return The global MaxScale configuration as a JSON object
     */
    json_t* maxscale_to_json(const char* host) const;

    /**
     * Get system information as a JSON object.
     *
     * @return System information as a JSON object.
     */
    json_t* system_to_json() const;

    class ParamAutoTune : public config::ParamStringList
    {
    public:
        using config::ParamStringList::ParamStringList;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const;
    };

    class ParamUsersRefreshTime : public config::ParamSeconds
    {
    public:
        using config::ParamSeconds::ParamSeconds;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const;
    };

    class ParamKeyManager : public config::ParamEnum<mxs::KeyManager::Type>
    {
    public:
        using config::ParamEnum<mxs::KeyManager::Type>::ParamEnum;

        bool takes_parameters() const override;

        bool validate_parameters(const std::string& value,
                                 const mxs::ConfigParameters& params,
                                 mxs::ConfigParameters* pUnrecognized = nullptr) const override;

        bool validate_parameters(const std::string& value,
                                 json_t* pParams,
                                 std::set<std::string>* pUnrecognized = nullptr) const override;

    private:
        template<class Params, class Unknown>
        bool do_validate_parameters(const std::string& value, Params params, Unknown* pUnrecognized) const;
    };

    class ParamLogThrottling : public config::ConcreteParam<ParamLogThrottling, MXB_LOG_THROTTLING>
    {
    public:
        using value_type = MXB_LOG_THROTTLING;

        ParamLogThrottling(config::Specification* pSpecification,
                           const char* zName,
                           const char* zDescription)
            : config::ConcreteParam<ParamLogThrottling, MXB_LOG_THROTTLING>(
                pSpecification, zName, zDescription,
                Modifiable::AT_RUNTIME,
                Param::OPTIONAL,
                MXB_LOG_THROTTLING {10, 1000, 10000})
        {
        }

        std::string type() const override final;

        std::string to_string(const value_type& value) const;
        bool        from_string(const std::string& value, value_type* pValue,
                                std::string* pMessage = nullptr) const;

        json_t* to_json(const value_type& value) const;
        bool    from_json(const json_t* pJson, value_type* pValue, std::string* pMessage = nullptr) const;
    };

    class LogThrottling : public config::ConcreteType<ParamLogThrottling>
    {
    public:
        LogThrottling(Configuration* pConfiguration,
                      const ParamLogThrottling* pParam,
                      std::function<void(value_type)> on_set = nullptr)
            : config::ConcreteType<ParamLogThrottling>(pConfiguration, pParam, on_set)
        {
        }
    };

    class ParamThreadsCount : public config::ParamCount
    {
    public:
        using config::ParamCount::ParamCount;

        bool from_string(const std::string& value_as_string,
                         value_type* pValue,
                         std::string* pMessage) const override final;
    };

    class ThreadsCount;

    using SessionDumpStatements = config::Enum<session_dump_statements_t>;
    using HttpMethod = maxbase::http::Method;
    using HttpMethods = std::vector<maxbase::http::Method>;


    std::vector<std::string> argv;                  /**< Copy of the argv array given to main. */

    // RUNTIME-modifiable automatically configured parameters.
    config::Bool          log_debug;                /**< Whether debug messages are logged. */
    config::Bool          log_info;                 /**< Whether info messages are logged. */
    config::Bool          log_notice;               /**< Whether notice messages are logged. */
    config::Bool          log_warning;              /**< Whether warning messages are logged. */
    LogThrottling         log_throttling;           /**< When and how to throttle logged messaged. */
    SessionDumpStatements dump_statements;          /**< Whether to dump last statements. */
    config::Count         session_trace;            /**< How entries stored to session trace log.*/
    config::Bool          ms_timestamp;             /**< Enable or disable high precision timestamps */
    config::Count         retain_last_statements;   /**< How many statements should be retained. */
    config::Bool          syslog;                   /**< Log to syslog */
    config::Bool          maxlog;                   /**< Log to MaxScale's own logs */
    config::Seconds       auth_conn_timeout;        /**< Connection timeout for the user authentication */
    config::Seconds       auth_read_timeout;        /**< Read timeout for the user authentication */
    config::Seconds       auth_write_timeout;       /**< Write timeout for the user authentication */
    config::Bool          passive;                  /**< True if MaxScale is in passive mode */
    config::Size          qc_cache_max_size;        /**< Maximum amount of memory used by qc */
    config::Bool          admin_log_auth_failures;  /**< Log admin interface authentication failures */
    config::Integer       query_retries;            /**< Number of times a interrupted query is
                                                     * retried */
    config::Seconds query_retry_timeout;            /**< Timeout for query retries */
    config::Seconds users_refresh_time;             /**< How often the users can be refreshed */
    config::Seconds users_refresh_interval;         /**< How often the users will be refreshed */
    config::Size    writeq_high_water;              /**< High water mark of dcb write queue */
    config::Size    writeq_low_water;               /**< Low water mark of dcb write queue */
    config::Integer max_auth_errors_until_block;    /**< Host is blocked once this limit is reached */
    config::Integer rebalance_threshold;            /**< If load of particular worker differs more than
                                                     * this % amount from load-average, rebalancing will
                                                     * be made.
                                                     */
    config::Milliseconds  rebalance_period;         /**< How often should rebalancing be made. */
    config::Count         rebalance_window;         /**< How many seconds should be taken into account. */
    config::Bool          skip_name_resolve;        /**< Reverse DNS lookups */
    mxs::KeyManager::Type key_manager;

    config::Bool     admin_audit_enabled;           /**< Enable logging to audit file */
    config::String   admin_audit_file;              /**< Audit file path */
    config::EnumList<HttpMethod> admin_audit_exclude_methods;/**< Which methods to exclude (e.g. GET) */

    // NON-modifiable automatically configured parameters.
    ParamAutoTune::value_type auto_tune;        /**< Vector of parameter names. */

    int64_t              n_threads;                    /**< Number of polling threads */
    int64_t              n_threads_max;                /**< Hard maximum for number of polling threads. */
    std::string          qc_name;                      /**< The name of the query classifier to load */
    std::string          qc_args;                      /**< Arguments for the query classifier */
    mxs::Parser::SqlMode qc_sql_mode;                  /**< The query classifier sql mode */
    std::string          admin_host;                   /**< Admin interface host */
    int64_t              admin_port;                   /**< Admin interface port */
    bool                 admin_auth;                   /**< Admin interface authentication */
    bool                 admin_enabled;                /**< Admin interface is enabled */
    std::string          admin_pam_rw_service;         /**< PAM service for read-write users */
    std::string          admin_pam_ro_service;         /**< PAM service for read-only users */

    std::string               admin_ssl_key;        /**< Admin SSL key */
    std::string               admin_ssl_cert;       /**< Admin SSL cert */
    std::string               admin_ssl_ca;         /**< Admin SSL CA cert */
    mxb::ssl_version::Version admin_ssl_version;    /**< Admin allowed SSL versions */
    mxs::JwtAlgo              admin_jwt_algorithm;  /**< JWT signature key */
    std::string               admin_jwt_key;        /**< Key used with symmetric JWT algorithms */
    seconds                   admin_jwt_max_age;    /**< Maximum JWT lifetime */
    std::string               admin_oidc_url;       /**< OIDC server for external JWTs */
    std::string               admin_verify_url;     /**< URL that points to a verification server */

    std::string  local_address;                 /**< Local address to use when connecting */
    bool         load_persisted_configs;        /**< Load persisted configuration files on startup */
    bool         persist_runtime_changes;       /**< Persist runtime changes */
    std::string  config_sync_cluster;           /**< Cluster used for config sync */
    std::string  config_sync_user;              /**< User used for config sync */
    std::string  config_sync_password;          /**< Password used for config sync */
    std::string  config_sync_db;                /**< Database for config sync */
    seconds      config_sync_timeout;           /**< Timeout for the config sync database operations */
    milliseconds config_sync_interval;          /**< How often to  sync the config */
    bool         log_warn_super_user;           /**< Log a warning if incoming client has super-priv. */
    bool         gui;                           /**< Enable admin GUI */
    bool         secure_gui;                    /**< Serve GUI only over HTTPS */
    std::string  debug;
    int64_t      max_read_amount;               /**< Max amount read before return to epoll_wait. */

    // The following will not be configured via the configuration mechanism.
    mxs::ConfigParameters key_manager_options;
    bool                  config_check;                         /**< Only check config */
    char                  release_string[RELEASE_STR_LENGTH];   /**< The release name string of the system */

    std::string sysname {"undefined"};      // Name of the implementation of the operating system
    std::string nodename {"undefined"};     // Name of this node on the network (i.e. hostname)
    std::string release {"undefined"};      // Current release level of this implementation
    std::string version {"undefined"};      // Current version level of this release
    std::string machine {"undefined"};      // Name of the hardware type the system is running on

    uint8_t mac_sha1[SHA_DIGEST_LENGTH];                /**< The SHA1 digest of an interface MAC address */

    mxb_log_target_t    log_target;                 /**< Log type */
    bool                substitute_variables;       /**< Should environment variables be substituted */
    using CacheProperties = mxs::CachingParser::Properties;
    CacheProperties     qc_cache_properties;        /**< The query classifier cache properties. */
    int64_t             promoted_at;                /**< Time when this Maxscale instance was
                                                    * promoted from a passive to an active */

    using config::Configuration::configure;

    // Overload that does some extra checks when starting up
    bool configure(const mxs::ConfigParameters& params,
                   mxs::ConfigParameters* pUnrecognized = nullptr) override;

private:
    Config(int argc, char** argv);

    void check_cpu_situation() const;
    void check_memory_situation() const;

    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

private:
    class Specification : public config::Specification
    {
    public:
        using config::Specification::Specification;

        bool validate(const Configuration* pConfig,
                      const mxs::ConfigParameters& params,
                      mxs::ConfigParameters* pUnrecognized = nullptr) const override final;
        bool validate(const Configuration* pConfig,
                      json_t* pJson,
                      std::set<std::string>* pUnrecognized = nullptr) const override final;

        bool post_validate(const Configuration* pConfig,
                           const mxs::ConfigParameters& params,
                           const std::map<std::string, mxs::ConfigParameters>& nested_params) const override
        {
            return do_post_validate(params, nested_params);
        }

        bool post_validate(const Configuration* pConfig,
                           json_t* pParams,
                           const std::map<std::string, json_t*>& nested_params) const override
        {
            return do_post_validate(pParams, nested_params);
        }

    private:
        template<class Params, class NestedParams>
        bool do_post_validate(Params& params, const NestedParams& nested_params) const;

        bool validate_events(const mxs::ConfigParameters& event_params) const;
        bool validate_events(json_t* pEvent_params) const;
        bool validate_event(const std::string& name, const std::string& value) const;
    };

    static Specification s_specification;

    static ParamAutoTune                                s_auto_tune;
    static config::ParamBool                            s_log_debug;
    static config::ParamBool                            s_log_info;
    static config::ParamBool                            s_log_notice;
    static config::ParamBool                            s_log_warning;
    static ParamLogThrottling                           s_log_throttling;
    static config::ParamEnum<session_dump_statements_t> s_dump_statements;
    static config::ParamCount                           s_session_trace;
    static config::ParamBool                            s_ms_timestamp;
    static config::ParamCount                           s_retain_last_statements;
    static config::ParamBool                            s_syslog;
    static config::ParamBool                            s_maxlog;
    static config::ParamSeconds                         s_auth_conn_timeout;
    static config::ParamSeconds                         s_auth_read_timeout;
    static config::ParamSeconds                         s_auth_write_timeout;
    static config::ParamDeprecated<config::ParamBool>   s_skip_permission_checks;
    static config::ParamBool                            s_passive;
    static config::ParamSize                            s_qc_cache_max_size;
    static config::ParamBool                            s_admin_log_auth_failures;
    static config::ParamInteger                         s_query_retries;
    static config::ParamSeconds                         s_query_retry_timeout;
    static ParamUsersRefreshTime                        s_users_refresh_time;
    static config::ParamSeconds                         s_users_refresh_interval;
    static config::ParamSize                            s_writeq_high_water;
    static config::ParamSize                            s_writeq_low_water;
    static config::ParamInteger                         s_max_auth_errors_until_block;
    static config::ParamInteger                         s_rebalance_threshold;
    static config::ParamMilliseconds                    s_rebalance_period;
    static config::ParamCount                           s_rebalance_window;
    static config::ParamBool                            s_skip_name_resolve;

    static ParamThreadsCount                            s_n_threads;
    static config::ParamCount                           s_n_threads_max;
    static config::ParamString                          s_qc_name;
    static config::ParamString                          s_qc_args;
    static config::ParamEnum<mxs::Parser::SqlMode>      s_qc_sql_mode;
    static config::ParamString                          s_admin_host;
    static config::ParamInteger                         s_admin_port;
    static config::ParamBool                            s_admin_auth;
    static config::ParamBool                            s_admin_enabled;
    static config::ParamString                          s_admin_pam_rw_service;
    static config::ParamString                          s_admin_pam_ro_service;
    static config::ParamPath                            s_admin_ssl_key;
    static config::ParamPath                            s_admin_ssl_cert;
    static config::ParamEnum<mxb::ssl_version::Version> s_admin_ssl_version;
    static config::ParamPath                            s_admin_ssl_ca;
    static config::ParamDeprecated<config::ParamAlias>  s_admin_ssl_ca_cert;// -> s_admin_ca
    static config::ParamEnum<mxs::JwtAlgo>              s_admin_jwt_algorithm;
    static config::ParamString                          s_admin_jwt_key;
    static config::ParamSeconds                         s_admin_jwt_max_age;
    static config::ParamString                          s_admin_oidc_url;
    static config::ParamBool                            s_admin_audit_enabled;
    static config::ParamString                          s_admin_audit_file;
    static config::ParamEnumList<HttpMethod>            s_admin_audit_exclude_methods;
    static config::ParamString                          s_admin_verify_url;
    static config::ParamString                          s_local_address;
    static config::ParamBool                            s_load_persisted_configs;
    static config::ParamBool                            s_persist_runtime_changes;
    static config::ParamString                          s_config_sync_cluster;
    static config::ParamString                          s_config_sync_user;
    static config::ParamPassword                        s_config_sync_password;
    static config::ParamString                          s_config_sync_db;
    static config::ParamSeconds                         s_config_sync_timeout;
    static config::ParamMilliseconds                    s_config_sync_interval;
    static config::ParamBool                            s_log_warn_super_user;
    static config::ParamBool                            s_gui;
    static config::ParamBool                            s_secure_gui;
    static config::ParamString                          s_debug;
    static config::ParamSize                            s_max_read_amount;
    static ParamKeyManager                              s_key_manager;
};
}
