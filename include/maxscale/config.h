#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * @file include/maxscale/config.h The configuration handling elements
 */

#include <maxscale/cdefs.h>

#include <limits.h>
#include <openssl/sha.h>
#include <sys/utsname.h>
#include <time.h>

#include <maxscale/modinfo.h>
#include <maxscale/jansson.h>
#include <maxscale/pcre2.h>
#include <maxscale/query_classifier.h>

#ifdef __cplusplus
#include <tr1/unordered_map>
#include <string>
// A mapping from a path to a percentage, e.g.: "/disk" -> 80.
typedef std::tr1::unordered_map<std::string, int32_t> MxsDiskSpaceThreshold;
#else
typedef void MxsDiskSpaceThreshold;
#endif

MXS_BEGIN_DECLS

/** Default port where the REST API listens */
#define DEFAULT_ADMIN_HTTP_PORT 8989
#define DEFAULT_ADMIN_HOST      "127.0.0.1"

#define RELEASE_STR_LENGTH      256
#define SYSNAME_LEN             256
#define MAX_ADMIN_USER_LEN      1024
#define MAX_ADMIN_PW_LEN        1024
#define MAX_ADMIN_HOST_LEN      1024

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
extern const char CN_ARG_MAX[];
extern const char CN_ARG_MIN[];
extern const char CN_ADMIN_AUTH[];
extern const char CN_ADMIN_ENABLED[];
extern const char CN_ADMIN_LOG_AUTH_FAILURES[];
extern const char CN_ADMIN_HOST[];
extern const char CN_ADMIN_PORT[];
extern const char CN_ADMIN_SSL_KEY[];
extern const char CN_ADMIN_SSL_CERT[];
extern const char CN_ADMIN_SSL_CA_CERT[];
extern const char CN_ATTRIBUTES[];
extern const char CN_AUTHENTICATOR[];
extern const char CN_AUTHENTICATOR_DIAGNOSTICS[];
extern const char CN_AUTHENTICATOR_OPTIONS[];
extern const char CN_AUTH_ALL_SERVERS[];
extern const char CN_AUTH_CONNECT_TIMEOUT[];
extern const char CN_AUTH_READ_TIMEOUT[];
extern const char CN_AUTH_WRITE_TIMEOUT[];
extern const char CN_AUTO[];
extern const char CN_CONNECTION_TIMEOUT[];
extern const char CN_DUMP_LAST_STATEMENTS[];
extern const char CN_DATA[];
extern const char CN_DEFAULT[];
extern const char CN_DESCRIPTION[];
extern const char CN_DISK_SPACE_THRESHOLD[];
extern const char CN_ENABLE_ROOT_USER[];
extern const char CN_FILTERS[];
extern const char CN_FILTER[];
extern const char CN_FILTER_DIAGNOSTICS[];
extern const char CN_GATEWAY[];
extern const char CN_ID[];
extern const char CN_INET[];
extern const char CN_LISTENER[];
extern const char CN_LISTENERS[];
extern const char CN_LOCALHOST_MATCH_WILDCARD_HOST[];
extern const char CN_LOG_AUTH_WARNINGS[];
extern const char CN_LOG_THROTTLING[];
extern const char CN_MAXSCALE[];
extern const char CN_MAX_CONNECTIONS[];
extern const char CN_MAX_RETRY_INTERVAL[];
extern const char CN_META[];
extern const char CN_METHOD[];
extern const char CN_MODULE[];
extern const char CN_MODULES[];
extern const char CN_MODULE_COMMAND[];
extern const char CN_MONITORS[];
extern const char CN_MONITOR[];
extern const char CN_MONITOR_DIAGNOSTICS[];
extern const char CN_MS_TIMESTAMP[];
extern const char CN_NAME[];
extern const char CN_NON_BLOCKING_POLLS[];
extern const char CN_OPTIONS[];
extern const char CN_PARAMETERS[];
extern const char CN_PASSIVE[];
extern const char CN_PASSWORD[];
extern const char CN_POLL_SLEEP[];
extern const char CN_PORT[];
extern const char CN_PROTOCOL[];
extern const char CN_QUERY_CLASSIFIER[];
extern const char CN_QUERY_CLASSIFIER_ARGS[];
extern const char CN_QUERY_RETRIES[];
extern const char CN_QUERY_RETRY_TIMEOUT[];
extern const char CN_RELATIONSHIPS[];
extern const char CN_LINKS[];
extern const char CN_REQUIRED[];
extern const char CN_RETAIN_LAST_STATEMENTS[];
extern const char CN_RETRY_ON_FAILURE[];
extern const char CN_ROUTER[];
extern const char CN_ROUTER_DIAGNOSTICS[];
extern const char CN_ROUTER_OPTIONS[];
extern const char CN_SELF[];
extern const char CN_SERVERS[];
extern const char CN_SERVER[];
extern const char CN_SERVICES[];
extern const char CN_SERVICE[];
extern const char CN_SESSIONS[];
extern const char CN_SKIP_PERMISSION_CHECKS[];
extern const char CN_SOCKET[];
extern const char CN_STATE[];
extern const char CN_SSL[];
extern const char CN_SSL_CA_CERT[];
extern const char CN_SSL_CERT[];
extern const char CN_SSL_CERT_VERIFY_DEPTH[];
extern const char CN_SSL_VERIFY_PEER_CERTIFICATE[];
extern const char CN_SSL_KEY[];
extern const char CN_SSL_VERSION[];
extern const char CN_STRIP_DB_ESC[];
extern const char CN_SUBSTITUTE_VARIABLES[];
extern const char CN_THREADS[];
extern const char CN_THREAD_STACK_SIZE[];
extern const char CN_TICKS[];
extern const char CN_TYPE[];
extern const char CN_UNIX[];
extern const char CN_USER[];
extern const char CN_USERS[];
extern const char CN_VERSION_STRING[];
extern const char CN_WEIGHTBY[];
extern const char CN_WRITEQ_HIGH_WATER[];
extern const char CN_WRITEQ_LOW_WATER[];

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
extern const char CN_LOG_TO_SHM[];

/**
 * The config parameter
 */
typedef struct config_parameter
{
    char                    *name;          /**< The name of the parameter */
    char                    *value;         /**< The value of the parameter */
    struct config_parameter *next;          /**< Next pointer in the linked list */
} MXS_CONFIG_PARAMETER;

/**
 * The config context structure, used to build the configuration
 * data during the parse process
 */
typedef struct config_context
{
    char                  *object;     /**< The name of the object being configured */
    MXS_CONFIG_PARAMETER  *parameters; /**< The list of parameter values */
    void                  *element;    /**< The element created from the data */
    bool                   was_persisted; /**< True if this object was persisted */
    struct config_context *next;       /**< Next pointer in the linked list */
} CONFIG_CONTEXT;

/**
 * The gateway global configuration data
 */
typedef struct
{
    bool          config_check;                        /**< Only check config */
    int           n_threads;                           /**< Number of polling threads */
    size_t        thread_stack_size;                   /**< The stack size of each worker thread */
    char          *version_string;                     /**< The version string of embedded db library */
    char          release_string[RELEASE_STR_LENGTH]; /**< The release name string of the system */
    char          sysname[SYSNAME_LEN];    /**< The OS name of the system */
    uint8_t       mac_sha1[SHA_DIGEST_LENGTH];         /**< The SHA1 digest of an interface MAC address */
    unsigned long id;                                  /**< MaxScale ID */
    unsigned int  n_nbpoll;                            /**< Tune number of non-blocking polls */
    unsigned int  pollsleep;                           /**< Wait time in blocking polls */
    int           syslog;                              /**< Log to syslog */
    int           maxlog;                              /**< Log to MaxScale's own logs */
    int           log_to_shm;                          /**< Write log-file to shared memory */
    unsigned int  auth_conn_timeout;                   /**< Connection timeout for the user authentication */
    unsigned int  auth_read_timeout;                   /**< Read timeout for the user authentication */
    unsigned int  auth_write_timeout;                  /**< Write timeout for the user authentication */
    bool          skip_permission_checks;              /**< Skip service and monitor permission checks */
    int32_t       passive;                             /**< True if MaxScale is in passive mode */
    int64_t       promoted_at;                         /**< Time when this Maxscale instance was
                                                        * promoted from a passive to an active */
    char          qc_name[PATH_MAX];                   /**< The name of the query classifier to load */
    char*         qc_args;                             /**< Arguments for the query classifier */
    QC_CACHE_PROPERTIES* qc_cache_properties;          /**< The query classifier cache properties. */
    qc_sql_mode_t qc_sql_mode;                         /**< The query classifier sql mode */
    char          admin_host[MAX_ADMIN_HOST_LEN];      /**< Admin interface host */
    uint16_t      admin_port;                          /**< Admin interface port */
    bool          admin_auth;                          /**< Admin interface authentication */
    bool          admin_enabled;                       /**< Admin interface is enabled */
    bool          admin_log_auth_failures;             /**< Log admin interface authentication failures */
    char          admin_ssl_key[PATH_MAX];             /**< Admin SSL key */
    char          admin_ssl_cert[PATH_MAX];            /**< Admin SSL cert */
    char          admin_ssl_ca_cert[PATH_MAX];         /**< Admin SSL CA cert */
    int           query_retries;                       /**< Number of times a interrupted query is retried */
    time_t        query_retry_timeout;                 /**< Timeout for query retries */
    bool          substitute_variables;                /**< Should environment variables be substituted */
    char*         local_address;                       /**< Local address to use when connecting */
    time_t        users_refresh_time;                  /**< How often the users can be refreshed */
    uint32_t      writeq_high_water;                   /**< High water mark of dcb write queue */
    uint32_t      writeq_low_water;                    /**< Low water mark of dcb write queue */
} MXS_CONFIG;

/**
 * @brief Get global MaxScale configuration
 *
 * @return The global configuration
 */
MXS_CONFIG* config_get_global_options();

/**
 * @brief Get a configuration parameter
 *
 * @param params List of parameters
 * @param name Name of parameter to get
 * @return The parameter or NULL if the parameter was not found
 */
MXS_CONFIG_PARAMETER* config_get_param(MXS_CONFIG_PARAMETER* params, const char* name);

/**
 * @brief Helper function for checking SSL parameters
 *
 * @param key Parameter name
 * @return True if the parameter is an SSL parameter
 */
bool config_is_ssl_parameter(const char *key);

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
bool config_param_is_valid(const MXS_MODULE_PARAM *params, const char *key,
                           const char *value, const CONFIG_CONTEXT *context);

/**
 * @brief Get a boolean value
 *
 * The existence of the parameter should be checked with config_get_param() before
 * calling this function to determine whether the return value represents an existing
 * value or a missing value.
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return The value as a boolean or false if none was found
 */
bool config_get_bool(const MXS_CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get an integer value
 *
 * This is used for both MXS_MODULE_PARAM_INT and MXS_MODULE_PARAM_COUNT.
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return The integer value of the parameter or 0 if no parameter was found
 */
int config_get_integer(const MXS_CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get a size in bytes
 *
 * The value can have either one of the IEC binary prefixes or SI prefixes as
 * a suffix. For example, the value 1Ki will be converted to 1024 bytes whereas
 * 1k will be converted to 1000 bytes. Supported SI suffix values are k, m, g and t
 * in both lower and upper case. Supported IEC binary suffix values are
 * Ki, Mi, Gi and Ti both in upper and lower case.
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return Number of bytes or 0 if no parameter was found
 */
uint64_t config_get_size(const MXS_CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get a string value
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return The raw string value or an empty string if no parameter was found
 */
const char* config_get_string(const MXS_CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get a enumeration value
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 * @param values All possible enumeration values
 *
 * @return The enumeration value converted to an int or -1 if the parameter was not found
 *
 * @note The enumeration values should not use -1 so that an undefined parameter is
 * detected. If -1 is used, config_get_param() should be used to detect whether
 * the parameter exists
 */
int config_get_enum(const MXS_CONFIG_PARAMETER *params, const char *key,
                    const MXS_ENUM_VALUE *values);

/**
 * @brief Get a service value
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return Pointer to configured service
 */
struct service* config_get_service(const MXS_CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get a server value
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return Pointer to configured server
 */
struct server* config_get_server(const MXS_CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get an array of servers. The caller should free the produced array,
 * but not the array elements.
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 * @param output Where to save the output
 * @return How many servers were found, equal to output array size
 */
int config_get_server_list(const MXS_CONFIG_PARAMETER *params, const char *key,
                           struct server*** output);

/**
 * Get a compiled regular expression and the ovector size of the pattern. The
 * return value should be freed by the caller.
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 * @param options PCRE2 compilation options
 * @param output_ovec_size Output for match data ovector size. On error,
 * nothing is written. If NULL, the parameter is ignored.
 * @return Compiled regex code on success, NULL otherwise
 */
pcre2_code* config_get_compiled_regex(const MXS_CONFIG_PARAMETER *params,
                                      const char *key, uint32_t options,
                                      uint32_t* output_ovec_size);

/**
 * Get multiple compiled regular expressions and the maximum ovector size of
 * the patterns. The returned regex codes should be freed by the caller.
 *
 * @param params List of configuration parameters
 * @param keys An array of parameter names. If an element is not found in @c params,
 * the corresponding spot in @c out_codes is set to NULL.
 * @param keys_size Size of both @c keys and @c out_arr
 * @param options PCRE2 compilation options
 * @param out_ovec_size If not NULL, the maximum ovector size of successfully
 * compiled patterns is written here.
 * @param out_codes An array of handles to compiled codes. The referenced pointers
 * will be set to point to the compiled codes. The array size must be equal to
 * @c keys array size and it must contain valid pointers.
 *
 * @return True, if all patterns given by @c keys were successfully compiled.
 * False otherwise.
 */
bool config_get_compiled_regexes(const MXS_CONFIG_PARAMETER *params,
                                 const char* keys[], int keys_size,
                                 uint32_t options, uint32_t* out_ovec_size,
                                 pcre2_code** out_codes[]);
/**
 * Parse a list of server names and write the results in an array of strings
 * with one server name in each. The output array and its elements should be
 * deallocated by the caller. The server names are not checked to be actual
 * configured servers.
 *
 * The output array may contain more elements than the the value returned, but these
 * extra elements are null and in the end of the array. If no server names were
 * parsed or if an error occurs, nothing is written to the output parameter.
 *
 * @param servers A list of server names
 * @param output_array Where to save the output
 * @return How many servers were found and set into the array. 0 on error or if
 * none were found.
 */
int config_parse_server_list(const char *servers, char ***output_array);

/**
 * @brief Get copy of parameter value if it is defined
 *
 * If a parameter with the name of @c key is defined in @c params, a copy of the
 * value of that parameter is returned. The caller must free the returned string.
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return Pointer to copy of value or NULL if the parameter was not found
 *
 * @note The use of this function should be avoided after startup as the function
 * will abort the process if memory allocation fails.
 */
char* config_copy_string(const MXS_CONFIG_PARAMETER *params, const char *key);

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
int config_truth_value(const char *value);

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
 * @brief Reload the configuration
 *
 * @return True if reloading was successful
 */
bool config_reload(void);

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
 * @brief  Get DCB write queue low water mark
 *
 * @return @return  Number of low water mark in bytes
 */
uint32_t config_writeq_low_water();

/**
 * @brief Interpret a @disk_space_threshold configuration string.
 *
 * @param disk_space_threshold  Data structure for holding disk space configuration.
 * @param config_value          Configuration value from the configuration file.
 *
 * @return True, if @ config_value was valid, false otherwise.
 *
 */
bool config_parse_disk_space_threshold(MxsDiskSpaceThreshold* disk_space_threshold,
                                       const char* config_value);

MXS_END_DECLS
