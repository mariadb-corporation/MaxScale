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
 * @file core/maxscale/config.h - The private config interface
 */

#include <maxscale/config.h>

#include <sstream>
#include <initializer_list>

#include <maxbase/jansson.h>
#include <maxscale/ssl.h>

#define DEFAULT_NBPOLLS             3      /**< Default number of non block polls before we block */
#define DEFAULT_POLLSLEEP           1000   /**< Default poll wait time (milliseconds) */
#define DEFAULT_NTHREADS            1      /**< Default number of polling threads */
#define DEFAULT_QUERY_RETRIES       1      /**< Number of retries for interrupted queries */
#define DEFAULT_QUERY_RETRY_TIMEOUT 5      /**< Timeout for query retries */
#define MIN_WRITEQ_HIGH_WATER       4096UL /**< Min high water mark of dcb write queue */
#define MIN_WRITEQ_LOW_WATER        512UL  /**< Min low water mark of dcb write queue */

// Default version string sent to clients
#define DEFAULT_VERSION_STRING "5.5.5-10.2.12 " MAXSCALE_VERSION "-maxscale"

/**
 * Maximum length for configuration parameter value.
 */
enum
{
    MAX_PARAM_LEN = 256
};

/** Object type specific parameter lists */
extern const MXS_MODULE_PARAM config_service_params[];
extern const MXS_MODULE_PARAM config_listener_params[];
extern const MXS_MODULE_PARAM config_monitor_params[];
extern const MXS_MODULE_PARAM config_filter_params[];
extern const MXS_MODULE_PARAM config_server_params[];
extern const char* config_pre_parse_global_params[];

/**
 * Initialize the configuration subsystem
 */
void config_init();

/**
 * Finalize the configuration subsystem
 */
void config_finish();

/**
 * Set the defaults for the global configuration options
 */
void config_set_global_defaults();

/**
 * @brief Generate default module parameters
 *
 * Adds any default parameters to @c ctx that aren't already in it.
 *
 * @param ctx    Configuration context where the parameters are added
 * @param params Module parameters
 */
void config_add_defaults(CONFIG_CONTEXT *ctx, const MXS_MODULE_PARAM *params);

char*                 config_clean_string_list(const char* str);
MXS_CONFIG_PARAMETER* config_clone_param(const MXS_CONFIG_PARAMETER* param);
bool                  config_load(const char *);
bool                  config_load_global(const char *filename);
void                  config_parameter_free(MXS_CONFIG_PARAMETER* p1);

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
 * @brief Replace an existing parameter
 *
 * @param obj Configuration context
 * @param key Parameter name
 * @param value Parameter value
 * @return True on success, false on memory allocation error
 */
bool config_replace_param(CONFIG_CONTEXT* obj, const char* key, const char* value);

/**
 * @brief Remove a parameter
 *
 * @param obj Configuration context
 * @param key Name of the parameter to remove
 */
void config_remove_param(CONFIG_CONTEXT* obj, const char* name);

/**
 * @brief Construct an SSL structure
 *
 * The SSL structure is used by both listeners and servers.
 *
 * @param name         Name of object being created (usually server or listener name)
 * @param params       Parameters to create SSL from
 * @param require_cert Whether certificates are required
 * @param dest         Pointer where initialized SSL structure is stored
 *
 * @return True on success, false on error
 */
bool config_create_ssl(const char* name, MXS_CONFIG_PARAMETER* params,
                       bool require_cert, SSL_LISTENER** dest);

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
 * @brief Add non-standard configuration parameters to a JSON object
 *
 * @param parameters List of configuration parameter values
 * @param param_info Configuration parameter type information
 * @param ignored_params List of parameters which should not be added to the output
 * @param output Output JSON object where the parameters are added
 */
void config_add_module_params_json(const MXS_CONFIG_PARAMETER* parameters, const MXS_MODULE_PARAM* param_info,
                                   const MXS_MODULE_PARAM* ignored_params, json_t* output);

/**
 * @brief Convert object names to correct format
 *
 * Check that object name contains no whitespace. If the name contains
 * whitespace, trim it, squeeze it and replace the remaining whitespace with
 * hyphens.
 *
 * @param name Object name to fix
 */
void fix_object_name(char *name);
void fix_object_name(std::string& name);

/**
 * @brief Serialize global options
 *
 * @return True if options were serialized successfully
 */
bool config_global_serialize();

/**
 * Export the configuration to a file
 *
 * @param filename Filename where the configuration will be written
 *
 * @return True if configuration was successfully exported
 */
bool export_config_file(const char* filename);

bool is_normal_server_parameter(const char *param);

/**
 * Converts a string into the corresponding value, interpreting
 * IEC or SI prefixes used as suffixes appropriately.
 *
 * @param value A numerical string, possibly suffixed by a IEC binary prefix or
 *              SI prefix.
 * @param dest  Pointer where the result is stored. If set to NULL, only the
 *              validity of value is checked.
 *
 * @return True on success, false on invalid input in which case contents of
 *         `dest` are left in an undefined state
 */
bool get_suffixed_size(const char* value, uint64_t* dest);

namespace maxscale
{

// Internal function
void dump_if_changed(const MXS_MODULE_PARAM* params, int file,
                     const std::string& key, const std::string& value);


/**
 * Dump a parameter into a file descriptor
 *
 * The function detects only literal matches to the string format default values.
 * If the string conversion results in an empty string, the value will not be
 * dumped. This can only happen if an empty string is passed as the value.
 *
 * The function writes a single key-value pair into the file and terminates
 * the line with a newline. This is intended to be used with configuration
 * dumping code in the core.
 *
 * Note: Does not work with enum type parameters, they'll get converted into
 * integers. Convert them to string format at the call site.
 *
 * @param file   File descriptor where the line is written
 * @param key    Name of the parameter
 * @param value  The parameter value
 * @param params List of module parameters to use
 */
template <class T>
inline void dump_param(int file, const std::string& key, const T value, std::initializer_list<const MXS_MODULE_PARAM*> params)
{
    std::stringstream ss;
    ss << value;
    auto strval = ss.str();

    if (!strval.empty())
    {
        // Don't dump empty values
        for (auto a : params)
        {
            dump_if_changed(a, file, key, strval);
        }
    }
}

// Specialization required to dump booleans in the same format as they used in
// the defaults. This requires that all defaults use either "true" or "false"
// for the values.
template <>
inline void dump_param(int file, const std::string& key, bool value, std::initializer_list<const MXS_MODULE_PARAM*> params)
{
    for (auto a : params)
    {
        dump_if_changed(a, file, key, value ? "true" : "false");
    }
}

}
