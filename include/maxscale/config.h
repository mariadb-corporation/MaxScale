#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file config.h The configuration handling elements
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 21/06/13     Mark Riddoch            Initial implementation
 * 07/05/14     Massimiliano Pinto      Added version_string to global configuration
 * 23/05/14     Massimiliano Pinto      Added id to global configuration
 * 17/10/14     Mark Riddoch            Added poll tuning configuration parameters
 * 05/03/15     Massimiliano Pinto      Added sysname, release, sha1_mac to gateway struct
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <limits.h>
#include <sys/utsname.h>
#include <openssl/sha.h>
#include <maxscale/gw_ssl.h>
#include <maxscale/modinfo.h>

MXS_BEGIN_DECLS

#define DEFAULT_NBPOLLS         3       /**< Default number of non block polls before we block */
#define DEFAULT_POLLSLEEP       1000    /**< Default poll wait time (milliseconds) */
#define _RELEASE_STR_LENGTH     256     /**< release len */
#define DEFAULT_NTHREADS        1 /**< Default number of polling threads */
/**
 * Maximum length for configuration parameter value.
 */
enum
{
    MAX_PARAM_LEN = 256
};

typedef enum
{
    UNDEFINED_TYPE     = 0x00,
    STRING_TYPE        = 0x01,
    COUNT_TYPE         = 0x02,
    PERCENT_TYPE       = 0x04,
    BOOL_TYPE          = 0x08,
    SQLVAR_TARGET_TYPE = 0x10
} config_param_type_t;

typedef enum
{
    TYPE_UNDEFINED = 0,
    TYPE_MASTER,
    TYPE_ALL
} target_t;

enum
{
    MAX_RLAG_NOT_AVAILABLE = -1,
    MAX_RLAG_UNDEFINED = -2
};

#define PARAM_IS_TYPE(p,t) ((p) & (t))

/**
 * The config parameter
 */
typedef struct config_parameter
{
    char                    *name;          /**< The name of the parameter */
    char                    *value;         /**< The value of the parameter */
    struct config_parameter *next;          /**< Next pointer in the linked list */
} CONFIG_PARAMETER;

/**
 * The config context structure, used to build the configuration
 * data during the parse process
 */
typedef struct config_context
{
    char                  *object;     /**< The name of the object being configured */
    CONFIG_PARAMETER      *parameters; /**< The list of parameter values */
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
} GATEWAY_CONF;


/**
 * @brief Creates an empty configuration context
 *
 * @param section Context name
 * @return New context or NULL on memory allocation failure
 */
CONFIG_CONTEXT* config_context_create(const char *section);

/**
 * @brief Free a configuration context
 *
 * @param context The context to free
 */
void config_context_free(CONFIG_CONTEXT *context);

/**
 * @brief Get a configuration parameter
 *
 * @param params List of parameters
 * @param name Name of parameter to get
 * @return The parameter or NULL if the parameter was not found
 */
CONFIG_PARAMETER* config_get_param(CONFIG_PARAMETER* params, const char* name);

/**
 * @brief Add a parameter to a configuration context
 *
 * @param obj Context where the parameter should be added
 * @param key Key to add
 * @param value Value for the key
 * @return True on success, false on memory allocation error
 */
bool config_add_param(CONFIG_CONTEXT* obj, const char* key, const char* value);

/**
 * @brief Append to an existing parameter
 *
 * @param obj Configuration context
 * @param key Parameter name
 * @param value Value to append to the parameter
 * @return True on success, false on memory allocation error
 */
bool config_append_param(CONFIG_CONTEXT* obj, const char* key, const char* value);

/**
 * @brief Check if all SSL parameters are defined
 *
 * Helper function to check whether all of the required SSL parameters are defined
 * in the configuration context. The checked parameters are 'ssl', 'ssl_key',
 * 'ssl_cert' and 'ssl_ca_cert'. The 'ssl' parameter must also have a value of
 * 'required'.
 *
 * @param obj Configuration context
 * @return True if all required parameters are present
 */
bool config_have_required_ssl_params(CONFIG_CONTEXT *obj);

/**
 * @brief Helper function for checking SSL parameters
 *
 * @param key Parameter name
 * @return True if the parameter is an SSL parameter
 */
bool config_is_ssl_parameter(const char *key);

/**
 * @brief Construct an SSL structure
 *
 * The SSL structure is used by both listeners and servers.
 *
 * TODO: Rename to something like @c config_construct_ssl
 *
 * @param obj Configuration context
 * @param require_cert Whether certificates are required
 * @param error_count Pointer to an int which is incremented for each error
 * @return New SSL_LISTENER structure or NULL on error
 */
SSL_LISTENER *make_ssl_structure(CONFIG_CONTEXT *obj, bool require_cert, int *error_count);

/**
 * @brief Check if a configuration parameter is valid
 *
 * If a module has declared parameters and parameters were given to the module,
 * the given parameters are compared to the expected ones. This function also
 * does preliminary type checking for various basic values as well as enumerations.
 *
 * @param module Module name
 * @param type Module type
 * @param key Parameter key
 * @param value Parameter value
 * @param context Configuration context or NULL for no context (uses runtime checks)
 *
 * @return True if the configuration parameter is valid
 */
bool config_param_is_valid(const char *module, const char *type, const char *key,
                           const char *value, const CONFIG_CONTEXT *context);

/**
 * @brief Get a boolean value
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return The value as a boolean
 */
bool config_get_bool(const CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get an integer value
 *
 * This is used for both MXS_MODULE_PARAM_INT and MXS_MODULE_PARAM_COUNT.
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return The integer value of the parameter
 */
int config_get_integer(const CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get a string value
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 *
 * @return The raw string value
 */
const char* config_get_string(const CONFIG_PARAMETER *params, const char *key);

/**
 * @brief Get a enumeration value
 *
 * @param params List of configuration parameters
 * @param key Parameter name
 * @param values All possible enumeration values
 *
 * @return The enumeration value converted to an int
 *
 * TODO: Allow multiple enumeration values
 */
int config_get_enum(const CONFIG_PARAMETER *params, const char *key, const MXS_ENUM_VALUE *values);

char*               config_clean_string_list(const char* str);
CONFIG_PARAMETER*   config_clone_param(const CONFIG_PARAMETER* param);
void                config_enable_feedback_task(void);
void                config_disable_feedback_task(void);
unsigned long       config_get_gateway_id(void);
GATEWAY_CONF*       config_get_global_options();
bool                config_load(const char *);
unsigned int        config_nbpolls();
unsigned int        config_pollsleep();
bool                config_reload();
int                 config_threadcount();
int                 config_truth_value(const char *);
void                config_parameter_free(CONFIG_PARAMETER* p1);
bool                is_internal_service(const char *router);

MXS_END_DECLS
