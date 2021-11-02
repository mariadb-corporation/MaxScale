/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file include/maxscale/config.h The configuration handling elements
 */

#include <maxscale/ccdefs.hh>

#include <unordered_map>
#include <string>
#include <limits.h>
#include <map>
#include <openssl/sha.h>
#include <sys/utsname.h>
#include <time.h>
#include <vector>

#include <maxbase/jansson.h>
#include <maxscale/modinfo.h>
#include <maxscale/pcre2.h>
#include <maxscale/query_classifier.hh>
#include <maxscale/server.hh>

class SERVICE;

/** Default port where the REST API listens */
#define DEFAULT_ADMIN_HTTP_PORT 8989
#define DEFAULT_ADMIN_HOST      "127.0.0.1"

#define RELEASE_STR_LENGTH 256
#define SYSNAME_LEN        256
#define MAX_ADMIN_USER_LEN 1024
#define MAX_ADMIN_PW_LEN   1024
#define MAX_ADMIN_HOST_LEN 1024

/** JSON Pointers to key parts of JSON objects */
#define MXS_JSON_PTR_DATA       "/data"
#define MXS_JSON_PTR_ID         "/data/id"
#define MXS_JSON_PTR_TYPE       "/data/type"
#define MXS_JSON_PTR_PARAMETERS "/data/attributes/parameters"

/** Pointers to relation lists */
#define MXS_JSON_PTR_RELATIONSHIPS          "/data/relationships"
#define MXS_JSON_PTR_RELATIONSHIPS_SERVERS  "/data/relationships/servers/data"
#define MXS_JSON_PTR_RELATIONSHIPS_SERVICES "/data/relationships/services/data"
#define MXS_JSON_PTR_RELATIONSHIPS_MONITORS "/data/relationships/monitors/data"
#define MXS_JSON_PTR_RELATIONSHIPS_FILTERS  "/data/relationships/filters/data"

/** Parameter value JSON Pointers */
#define MXS_JSON_PTR_PARAM_PORT                  MXS_JSON_PTR_PARAMETERS "/port"
#define MXS_JSON_PTR_PARAM_ADDRESS               MXS_JSON_PTR_PARAMETERS "/address"
#define MXS_JSON_PTR_PARAM_SOCKET                MXS_JSON_PTR_PARAMETERS "/socket"
#define MXS_JSON_PTR_PARAM_PROTOCOL              MXS_JSON_PTR_PARAMETERS "/protocol"
#define MXS_JSON_PTR_PARAM_AUTHENTICATOR         MXS_JSON_PTR_PARAMETERS "/authenticator"
#define MXS_JSON_PTR_PARAM_AUTHENTICATOR_OPTIONS MXS_JSON_PTR_PARAMETERS "/authenticator_options"
#define MXS_JSON_PTR_PARAM_SSL_KEY               MXS_JSON_PTR_PARAMETERS "/ssl_key"
#define MXS_JSON_PTR_PARAM_SSL_CERT              MXS_JSON_PTR_PARAMETERS "/ssl_cert"
#define MXS_JSON_PTR_PARAM_SSL_CA_CERT           MXS_JSON_PTR_PARAMETERS "/ssl_ca_cert"
#define MXS_JSON_PTR_PARAM_SSL_VERSION           MXS_JSON_PTR_PARAMETERS "/ssl_version"
#define MXS_JSON_PTR_PARAM_SSL_CERT_VERIFY_DEPTH MXS_JSON_PTR_PARAMETERS "/ssl_cert_verify_depth"
#define MXS_JSON_PTR_PARAM_SSL_VERIFY_PEER_CERT  MXS_JSON_PTR_PARAMETERS "/ssl_verify_peer_certificate"

/** Non-parameter JSON pointers */
#define MXS_JSON_PTR_ROUTER   "/data/attributes/router"
#define MXS_JSON_PTR_MODULE   "/data/attributes/module"
#define MXS_JSON_PTR_PASSWORD "/data/attributes/password"
#define MXS_JSON_PTR_ACCOUNT  "/data/attributes/account"

/**
 * Common configuration parameters names
 *
 * All of the constants resolve to a lowercase version without the CN_ prefix.
 * For example CN_PASSWORD resolves to the static string "password".
 */
extern const char CN_ACCOUNT[];
extern const char CN_ADDRESS[];
extern const char CN_ADMIN_AUTH[];
extern const char CN_ADMIN_ENABLED[];
extern const char CN_ADMIN_HOST[];
extern const char CN_ADMIN_LOG_AUTH_FAILURES[];
extern const char CN_ADMIN_PORT[];
extern const char CN_ADMIN_SSL_CA_CERT[];
extern const char CN_ADMIN_SSL_CERT[];
extern const char CN_ADMIN_SSL_KEY[];
extern const char CN_ARG_MAX[];
extern const char CN_ARG_MIN[];
extern const char CN_ARGUMENTS[];
extern const char CN_ATTRIBUTES[];
extern const char CN_AUTH_ALL_SERVERS[];
extern const char CN_AUTH_CONNECT_TIMEOUT[];
extern const char CN_AUTH_READ_TIMEOUT[];
extern const char CN_AUTH_WRITE_TIMEOUT[];
extern const char CN_AUTHENTICATOR_DIAGNOSTICS[];
extern const char CN_AUTHENTICATOR_OPTIONS[];
extern const char CN_AUTHENTICATOR[];
extern const char CN_AUTO[];
extern const char CN_BACKEND_CONNECT_ATTEMPTS[];
extern const char CN_BACKEND_CONNECT_TIMEOUT[];
extern const char CN_BACKEND_READ_TIMEOUT[];
extern const char CN_BACKEND_WRITE_TIMEOUT[];
extern const char CN_CACHE_SIZE[];
extern const char CN_CACHE[];
extern const char CN_CLASSIFICATION[];
extern const char CN_CLASSIFY[];
extern const char CN_CLUSTER[];
extern const char CN_CONNECTION_TIMEOUT[];
extern const char CN_DATA[];
extern const char CN_DEFAULT[];
extern const char CN_DESCRIPTION[];
extern const char CN_DISK_SPACE_CHECK_INTERVAL[];
extern const char CN_DISK_SPACE_THRESHOLD[];
extern const char CN_DUMP_LAST_STATEMENTS[];
extern const char CN_ENABLE_ROOT_USER[];
extern const char CN_EVENTS[];
extern const char CN_EXTRA_PORT[];
extern const char CN_FIELDS[];
extern const char CN_FILTER_DIAGNOSTICS[];
extern const char CN_FILTER[];
extern const char CN_FILTERS[];
extern const char CN_FORCE[];
extern const char CN_FUNCTIONS[];
extern const char CN_GATEWAY[];
extern const char CN_HAS_WHERE_CLAUSE[];
extern const char CN_HITS[];
extern const char CN_ID[];
extern const char CN_INET[];
extern const char CN_JOURNAL_MAX_AGE[];
extern const char CN_LINKS[];
extern const char CN_LOAD_PERSISTED_CONFIGS[];
extern const char CN_LISTENER[];
extern const char CN_LISTENERS[];
extern const char CN_LOCALHOST_MATCH_WILDCARD_HOST[];
extern const char CN_LOG_AUTH_WARNINGS[];
extern const char CN_LOG_THROTTLING[];
extern const char CN_MAX_AUTH_ERRORS_UNTIL_BLOCK[];
extern const char CN_MAX_CONNECTIONS[];
extern const char CN_MAX_RETRY_INTERVAL[];
extern const char CN_MAXSCALE[];
extern const char CN_META[];
extern const char CN_METHOD[];
extern const char CN_MODULE_COMMAND[];
extern const char CN_MODULE[];
extern const char CN_MODULES[];
extern const char CN_MONITOR[];
extern const char CN_MONITOR_DIAGNOSTICS[];
extern const char CN_MONITOR_INTERVAL[];
extern const char CN_MONITORS[];
extern const char CN_MS_TIMESTAMP[];
extern const char CN_NAME[];
extern const char CN_NET_WRITE_TIMEOUT[];
extern const char CN_NON_BLOCKING_POLLS[];
extern const char CN_OPERATION[];
extern const char CN_OPTIONS[];
extern const char CN_PARAMETERS[];
extern const char CN_PARSE_RESULT[];
extern const char CN_PASSIVE[];
extern const char CN_PASSWORD[];
extern const char CN_POLL_SLEEP[];
extern const char CN_PORT[];
extern const char CN_PROTOCOL[];
extern const char CN_QUERY_CLASSIFIER_ARGS[];
extern const char CN_QUERY_CLASSIFIER_CACHE_SIZE[];
extern const char CN_QUERY_CLASSIFIER[];
extern const char CN_QUERY_RETRIES[];
extern const char CN_QUERY_RETRY_TIMEOUT[];
extern const char CN_RELATIONSHIPS[];
extern const char CN_REQUIRED[];
extern const char CN_RETAIN_LAST_STATEMENTS[];
extern const char CN_RETRY_ON_FAILURE[];
extern const char CN_ROUTER_DIAGNOSTICS[];
extern const char CN_ROUTER_OPTIONS[];
extern const char CN_ROUTER[];
extern const char CN_SCRIPT[];
extern const char CN_SCRIPT_TIMEOUT[];
extern const char CN_SELF[];
extern const char CN_SERVER[];
extern const char CN_SERVERS[];
extern const char CN_SERVICE[];
extern const char CN_SERVICES[];
extern const char CN_SESSION_TRACK_TRX_STATE[];
extern const char CN_SESSIONS[];
extern const char CN_SESSION_TRACE[];
extern const char CN_SKIP_PERMISSION_CHECKS[];
extern const char CN_SOCKET[];
extern const char CN_SSL_CA_CERT[];
extern const char CN_SSL_CERT_VERIFY_DEPTH[];
extern const char CN_SSL_CIPHER[];
extern const char CN_SSL_CERT[];
extern const char CN_SSL_KEY[];
extern const char CN_SSL_VERIFY_PEER_CERTIFICATE[];
extern const char CN_SSL_VERSION[];
extern const char CN_SSL[];
extern const char CN_STATE[];
extern const char CN_STATEMENT[];
extern const char CN_STATEMENTS[];
extern const char CN_STRIP_DB_ESC[];
extern const char CN_SUBSTITUTE_VARIABLES[];
extern const char CN_THREAD_STACK_SIZE[];
extern const char CN_THREADS[];
extern const char CN_TICKS[];
extern const char CN_TYPE_MASK[];
extern const char CN_TYPE[];
extern const char CN_UNIX[];
extern const char CN_USER[];
extern const char CN_USERS[];
extern const char CN_VERSION_STRING[];
extern const char CN_WEIGHTBY[];
extern const char CN_WRITEQ_HIGH_WATER[];
extern const char CN_WRITEQ_LOW_WATER[];
extern const char CN_YES[];

/*
 * Global configuration items that are read (or pre_parsed) to be available for
 * subsequent configuration reading. @see config_pre_parse_global_params.
 */
extern const char CN_LOGDIR[];
extern const char CN_LIBDIR[];
extern const char CN_PIDDIR[];
extern const char CN_DATADIR[];
extern const char CN_CACHEDIR[];
extern const char CN_LANGUAGE[];
extern const char CN_EXECDIR[];
extern const char CN_CONNECTOR_PLUGINDIR[];
extern const char CN_PERSISTDIR[];
extern const char CN_MODULE_CONFIGDIR[];
extern const char CN_SYSLOG[];
extern const char CN_MAXLOG[];
extern const char CN_LOG_AUGMENTATION[];

namespace maxscale
{

namespace config
{

enum DurationInterpretation
{
    INTERPRET_AS_SECONDS,
    INTERPRET_AS_MILLISECONDS
};

enum DurationUnit
{
    DURATION_IN_HOURS,
    DURATION_IN_MINUTES,
    DURATION_IN_SECONDS,
    DURATION_IN_MILLISECONDS,
    DURATION_IN_DEFAULT
};
}
}

/**
 * Config parameter container. Typically includes all parameters of a single configuration file section
 * such as a server or filter.
 */
class MXS_CONFIG_PARAMETER
{
public:
    using ContainerType = std::map<std::string, std::string>;

    /**
     * Get value of key as string.
     *
     * @param key Parameter name
     * @return Parameter value. Empty string if key not found.
     */
    std::string get_string(const std::string& key) const;

    /**
     * @brief Get copy of parameter value if it is defined
     *
     * If a parameter with the name of @c key is defined in @c params, a copy of the
     * value of that parameter is returned. The caller must free the returned string.
     *
     * @param key Parameter name
     * @return Pointer to copy of value or NULL if the parameter was not found
     *
     * @note The use of this function should be avoided after startup as the function
     * will abort the process if memory allocation fails.
     */
    char* get_c_str_copy(const std::string& key) const;

    /**
     * Get an integer value. Should be used for both MXS_MODULE_PARAM_INT and MXS_MODULE_PARAM_COUNT
     * parameter types.
     *
     * @param key Parameter name
     * @return Parameter parsed to integer. 0 if key was not found.
     */
    int64_t get_integer(const std::string& key) const;

    /**
     * Get a enumeration value.
     *
     * @param key Parameter name
     * @param enum_mapping Enum string->integer mapping
     * @return The enumeration value converted to an int or -1 if the parameter was not found
     *
     * @note The enumeration values should not use -1 so that an undefined parameter is
     * detected. If -1 is used, config_get_param() should be used to detect whether
     * the parameter exists
     */
    int64_t get_enum(const std::string& key, const MXS_ENUM_VALUE* enum_mapping) const;

    /**
     * @brief Get a boolean value
     *
     * The existence of the parameter should be checked with config_get_param() before
     * calling this function to determine whether the return value represents an existing
     * value or a missing value.
     *
     * @param key Parameter name
     * @return The value as a boolean or false if none was found
     */
    bool get_bool(const std::string& key) const;

    /**
     * @brief Get a size in bytes
     *
     * The value can have either one of the IEC binary prefixes or SI prefixes as
     * a suffix. For example, the value 1Ki will be converted to 1024 bytes whereas
     * 1k will be converted to 1000 bytes. Supported SI suffix values are k, m, g and t
     * in both lower and upper case. Supported IEC binary suffix values are
     * Ki, Mi, Gi and Ti both in upper and lower case.
     *
     * @param key Parameter name
     * @return Number of bytes or 0 if no parameter was found
     */
    uint64_t get_size(const std::string& key) const;

    /**
     * @brief Get a duration.
     *
     * Should be used for MXS_MODULE_PARAM_DURATION parameter types.
     *
     * @param key             Parameter name.
     * @param interpretation  How a value NOT having a unit suffix should be interpreted.
     *
     * @return Duration in milliseconds; 0 if the parameter is not found.
     */
    std::chrono::milliseconds get_duration_in_ms(const std::string& key,
                                                 mxs::config::DurationInterpretation interpretation) const;

    /**
     * @brief Get a duration in a specific unit.
     *
     * @param key  Parameter name
     *
     * @return The duration in the desired unit.
     *
     * @note The type the function is specialized with dictates how values without a
     *       suffix should be interpreted; if @c std::chrono::seconds, they will be
     *       interpreted as seconds, if @c std::chrono::milliseconds, they will be
     *       interpreted as milliseconds.
     *
     * @note There is no default implementation, but only specializations for
     *       @c std::chrono::seconds and @c std::chrono::milliseconds.
     */
    template<class T>
    T get_duration(const std::string& key) const = delete;

    /**
     * @brief Get a service value
     *
     * @param key Parameter name
     * @return Pointer to configured service
     */
    SERVICE* get_service(const std::string& key) const;

    /**
     * @brief Get a server value
     *
     * @param key Parameter name
     * @return Pointer to configured server
     */
    SERVER* get_server(const std::string& key) const;

    /**
     * Get an array of servers. The value is expected to be a comma-separated list of server names.
     *
     * @param key Parameter name
     * @param name_error_out If a server name was not found, it is written here. Only the first such name
     * is written.
     * @return Found servers. If even one server name was invalid, the array will be empty.
     */
    std::vector<SERVER*> get_server_list(const std::string& key, std::string* name_error_out = nullptr) const;

    /**
     * Get a compiled regular expression and the ovector size of the pattern. The
     * return value should be freed by the caller.
     *
     * @param key Parameter name
     * @param options PCRE2 compilation options
     * @param output_ovec_size Output for match data ovector size. On error,
     * nothing is written. If NULL, the parameter is ignored.
     * @return Compiled regex code on success. NULL if key was not found or compilation failed.
     */
    std::unique_ptr<pcre2_code>
    get_compiled_regex(const std::string& key, uint32_t options, uint32_t* output_ovec_size) const;

    /**
     * Get multiple compiled regular expressions and the maximum ovector size of
     * the patterns. The returned regex codes should be freed by the caller.
     *
     * @param keys An array of parameter keys.
     * @param options PCRE2 compilation options
     * @param ovec_size_out If not null, the maximum ovector size of successfully
     * compiled patterns is written here.
     * @param compile_error_out If not null, is set to true if a pattern compilation failed.
     * @return Array of compiled patterns, one element for each key. If a key is not found or the pattern
     * cannot be compiled, the corresponding element will be null.
     */
    std::vector<std::unique_ptr<pcre2_code>>
    get_compiled_regexes(const std::vector<std::string>& keys, uint32_t options,
                         uint32_t* ovec_size_out, bool* compile_error_out);

    /**
     * Check if a key exists.
     *
     * @param key Parameter name
     * @return True if key was found
     */
    bool contains(const std::string& key) const;

    /**
     * Check if any of the given keys are defined
     *
     * @param keys The keys to check
     *
     * @return True if at least one of the keys is defined
     */
    bool contains_any(const std::initializer_list<std::string>& keys) const
    {
        return std::any_of(keys.begin(), keys.end(), [this](const std::string& a) {
                               return contains(a);
                           });
    }

    /**
     * Set a key-value combination. If the key doesn't exist, it is added. The function is static
     * to handle the special case of params being empty. This is needed until the config management
     * has been properly refactored.
     *
     * @param key Parameter key
     * @param value Value to set
     */
    void set(const std::string& key, const std::string& value);

    /**
     * Copy all key-value pairs from a set to this container. If a key doesn't exist, it is added.
     *
     * @param source Parameters to copy
     */
    void set_multiple(const MXS_CONFIG_PARAMETER& source);

    void set_from_list(std::vector<std::pair<std::string, std::string>> list,
                       const MXS_MODULE_PARAM* module_params = NULL);

    /**
     * Remove a key-value pair from the container.
     *
     * @param key Key to remove
     */
    void remove(const std::string& key);

    void clear();
    bool empty() const;

    ContainerType::const_iterator begin() const;
    ContainerType::const_iterator end() const;

private:
    ContainerType m_contents;
};

template<>
inline std::chrono::milliseconds
MXS_CONFIG_PARAMETER::get_duration<std::chrono::milliseconds>(const std::string& key) const
{
    return get_duration_in_ms(key, mxs::config::INTERPRET_AS_MILLISECONDS);
}

template<>
inline std::chrono::seconds
MXS_CONFIG_PARAMETER::get_duration<std::chrono::seconds>(const std::string& key) const
{
    std::chrono::milliseconds ms = get_duration_in_ms(key, mxs::config::INTERPRET_AS_SECONDS);
    return std::chrono::duration_cast<std::chrono::seconds>(ms);
}


/**
 * The config context structure, used to build the configuration
 * data during the parse process
 */
class CONFIG_CONTEXT
{
public:
    CONFIG_CONTEXT(const std::string& section = "");

    std::string          m_name;            /**< The name of the object being configured */
    MXS_CONFIG_PARAMETER m_parameters;      /**< The list of parameter values */
    bool                 m_was_persisted;   /**< True if this object was persisted */
    CONFIG_CONTEXT*      m_next;            /**< Next pointer in the linked list */

    const char* name() const
    {
        return m_name.c_str();
    }
};

/**
 * The gateway global configuration data
 */
struct MXS_CONFIG
{
    bool    config_check;                               /**< Only check config */
    int     n_threads;                                  /**< Number of polling threads */
    size_t  thread_stack_size;                          /**< The stack size of each worker thread */
    char    release_string[RELEASE_STR_LENGTH];         /**< The release name string of the system */
    char    sysname[SYSNAME_LEN];                       /**< The OS name of the system */
    uint8_t mac_sha1[SHA_DIGEST_LENGTH];                /**< The SHA1 digest of an interface MAC address */

    unsigned int n_nbpoll;                              /**< Tune number of non-blocking polls */
    unsigned int pollsleep;                             /**< Wait time in blocking polls */
    int          syslog;                                /**< Log to syslog */
    int          maxlog;                                /**< Log to MaxScale's own logs */
    time_t       auth_conn_timeout;                     /**< Connection timeout for the user
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
    uint64_t writeq_high_water;                         /**< High water mark of dcb write queue */
    uint64_t writeq_low_water;                          /**< Low water mark of dcb write queue */
    char     peer_hosts[MAX_ADMIN_HOST_LEN];            /**< The protocol, address and port for peers
                                                         * (currently only one) */
    char             peer_user[MAX_ADMIN_HOST_LEN];     /**< Username for maxscale-to-maxscale traffic */
    char             peer_password[MAX_ADMIN_HOST_LEN]; /**< Password for maxscale-to-maxscale traffic */
    mxb_log_target_t log_target;                        /**< Log type */
    bool             load_persisted_configs;            /**< Load persisted configuration files on startup */
    int              max_auth_errors_until_block;       /**< Host is blocked once this limit is reached */
};

/**
 * @brief Get global MaxScale configuration
 *
 * @return The global configuration
 */
MXS_CONFIG* config_get_global_options();

/**
 * @brief Helper function for checking SSL parameters
 *
 * @param key Parameter name
 * @return True if the parameter is an SSL parameter
 */
bool config_is_ssl_parameter(const char* key);

/**
 * @brief Check if a configuration parameter is valid
 *
 * If a module has declared parameters and parameters were given to the module,
 * the given parameters are compared to the expected ones. This function also
 * does preliminary type checking for various basic values as well as enumerations.
 *
 * @param params Module parameters
 * @param key Parameter key
 * @param value Parameter value
 * @param context Configuration context or NULL for no context (uses runtime checks)
 *
 * @return True if the configuration parameter is valid
 */
bool config_param_is_valid(const MXS_MODULE_PARAM* params,
                           const char* key,
                           const char* value,
                           const CONFIG_CONTEXT* context);

/**
 * Break a comma-separated list into a string array. Removes whitespace from list items.
 *
 * @param list_string A list of items
 * @return The array
 */
std::vector<std::string> config_break_list_string(const std::string& list_string);

/**
 * @brief Convert string truth value
 *
 * Used for truth values with @c 1, @c yes or @c true for a boolean true value and @c 0, @c no
 * or @c false for a boolean false value.
 *
 * @param str String to convert to a truth value
 *
 * @return 1 if @c value is true, 0 if value is false and -1 if the value is not
 * a valid truth value
 */
int config_truth_value(const char* value);

/**
 * @brief Get worker thread count
 *
 * @return Number of worker threads
 */
int config_threadcount(void);

/**
 * @brief Get thread stack size
 *
 * @return The configured worker thread stack size.
 */
size_t config_thread_stack_size(void);


/**
 * @brief Get number of non-blocking polls
 *
 * @return Number of non-blocking polls
 */
unsigned int config_nbpolls(void);

/**
 * @brief Get poll sleep interval
 *
 * @return The time each thread waits for a blocking poll
 */
unsigned int config_pollsleep(void);

/**
 * @brief List all path parameters as JSON
 *
 * @param host Hostname of this server
 * @return JSON object representing the paths used by MaxScale
 */
json_t* config_maxscale_to_json(const char* host);

/**
 * @brief  Get DCB write queue high water mark
 *
 * @return  Number of high water mark in bytes
 */
uint32_t config_writeq_high_water();

/**
 * Set writeq high water mark
 *
 * @param size The high water mark in bytes
 *
 * @return True if the parameter was larger than MIN_WRITEQ_HIGH_WATER
 */
bool config_set_writeq_high_water(uint32_t size);

/**
 * @brief  Get DCB write queue low water mark
 *
 * @return @return  Number of low water mark in bytes
 */
uint32_t config_writeq_low_water();

/**
 * Set writeq low water mark
 *
 * @param size The low water mark in bytes
 *
 * @return True if the parameter was larger than MIN_WRITEQ_LOW_WATER
 */
bool config_set_writeq_low_water(uint32_t size);

/**
 * @brief Interpret a @disk_space_threshold configuration string.
 *
 * @param disk_space_threshold  Data structure for holding disk space configuration.
 * @param config_value          Configuration value from the configuration file.
 *
 * @return True, if @ config_value was valid, false otherwise.
 *
 */
bool config_parse_disk_space_threshold(SERVER::DiskSpaceLimits* disk_space_threshold,
                                       const char* config_value);

/**
 * @brief Check whether section/object name is valid.
 *
 * @param name     The name to be checked.
 * @param reason   If non-null, will in case the name is not valid contain
 *                 the reason when the function returns.
 *
 * @return True, if the name is valid, false otherwise.
 */
bool config_is_valid_name(const char* name, std::string* reason = nullptr);

inline bool config_is_valid_name(const std::string& name, std::string* reason = nullptr)
{
    return config_is_valid_name(name.c_str());
}

// TEMPORARILY EXPOSED.
bool check_path_parameter(const MXS_MODULE_PARAM* params, const char* value);

/**
 * Converts a string into milliseconds, intepreting in a case-insensitive manner
 * an 'h'-suffix to indicate hours, an 'm'-suffix to indicate minutes, an
 * 's'-suffix to indicate seconds and an 'ms'-suffix to indicate milliseconds.
 *
 * @param zValue          A numerical string, possibly suffixed by 'h', 'm',
 *                        's' or 'ms'.
 * @param interpretation  How a value lacking a specific suffix should be interpreted.
 * @param pDuration       Pointer, if non-NULL, where the result is stored.
 * @param pUnit           Pointer, if non-NULL, where the detected unit is stored.
 *
 * @return True on success, false on invalid input in which case @c pUnit and
 *         @c pDuration will not be modified.
 */
bool get_suffixed_duration(const char* zValue,
                           mxs::config::DurationInterpretation interpretation,
                           std::chrono::milliseconds* pDuration,
                           mxs::config::DurationUnit* pUnit = nullptr);

/**
 * Converts a string into milliseconds, intepreting in a case-insensitive manner
 * an 'h'-suffix to indicate hours, an 'm'-suffix to indicate minutes, an
 * 's'-suffix to indicate seconds and an 'ms'-suffix to indicate milliseconds.
 *
 * A value lacking a specific suffix will be interpreted as milliseconds.
 *
 * @param zValue     A numerical string, possibly suffixed by 'h', 'm',
 *                   's' or 'ms'.
 * @param pDuration  Pointer, if non-NULL, where the result is stored.
 * @param pUnit      Pointer, if non-NULL, where the detected unit is stored.
 *
 * @return True on success, false on invalid input in which case @c pUnit and
 *         @c pDuration will not be modified.
 */
inline bool get_suffixed_duration(const char* zValue,
                                  std::chrono::milliseconds* pDuration,
                                  mxs::config::DurationUnit* pUnit = nullptr)
{
    return get_suffixed_duration(zValue, mxs::config::INTERPRET_AS_MILLISECONDS, pDuration, pUnit);
}

/**
 * Converts a string into seconds, intepreting in a case-insensitive manner
 * an 'h'-suffix to indicate hours, an 'm'-suffix to indicate minutes, an
 * 's'-suffix to indicate seconds and an 'ms'-suffix to indicate milliseconds.
 *
 * A value lacking a specific suffix will be interpreted as seconds.
 *
 * @param zValue     A numerical string, possibly suffixed by 'h', 'm',
 *                   's' or 'ms'.
 * @param pDuration  Pointer, if non-NULL, where the result is stored.
 * @param pUnit      Pointer, if non-NULL, where the detected unit is stored.
 *
 * @return True on success, false on invalid input in which case @c pUnit and
 *         @c pDuration will not be modified.
 */
inline bool get_suffixed_duration(const char* zValue,
                                  std::chrono::seconds* pDuration,
                                  mxs::config::DurationUnit* pUnit = nullptr)
{
    std::chrono::milliseconds ms;

    bool rv = get_suffixed_duration(zValue, mxs::config::INTERPRET_AS_SECONDS, &ms, pUnit);

    if (rv)
    {
        *pDuration = std::chrono::duration_cast<std::chrono::seconds>(ms);
    }

    return rv;
}
