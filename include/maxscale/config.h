#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
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

#include <maxscale/modinfo.h>

MXS_BEGIN_DECLS

#define _RELEASE_STR_LENGTH     256     /**< release len */

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
    char          *version_string;                     /**< The version string of embedded db library */
    char          release_string[_RELEASE_STR_LENGTH]; /**< The release name string of the system */
    char          sysname[_UTSNAME_SYSNAME_LENGTH];    /**< The OS name of the system */
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
    char          qc_name[PATH_MAX];                   /**< The name of the query classifier to load */
    char*         qc_args;                             /**< Arguments for the query classifier */
    int           query_retries;                       /**< Number of times a interrupted query is retried */
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
 * @brief Enable feedback task
 */
void config_enable_feedback_task(void);

/**
 * @brief Disable feedback task
 */
void config_disable_feedback_task(void);


/** TODO: Add new capability that allows skipping of permission checks */
bool is_internal_service(const char *router);

/**
 * @brief Reload the configuration
 *
 * @return True if reloading was successful
 */
bool config_reload(void);

MXS_END_DECLS
