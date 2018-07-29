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

#include <maxscale/ssl.h>
#include <maxscale/jansson.h>

#define DEFAULT_NBPOLLS             3    /**< Default number of non block polls before we block */
#define DEFAULT_POLLSLEEP           1000 /**< Default poll wait time (milliseconds) */
#define DEFAULT_NTHREADS            1    /**< Default number of polling threads */
#define DEFAULT_QUERY_RETRIES       0    /**< Number of retries for interrupted queries */
#define DEFAULT_QUERY_RETRY_TIMEOUT 5    /**< Timeout for query retries */
#define MIN_WRITEQ_HIGH_WATER       4096 /**< Min high water mark of dcb write queue */
#define MIN_WRITEQ_LOW_WATER        512  /**< Min low water mark of dcb write queue */

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
 * @brief Add non-standard module type parameters to a JSON object
 *
 * @param mod         Module whose parameters are inspected
 * @param parameters  List of configuration parameters for the module
 * @param type_params NULL terminated list of default module type parameters
 * @param output      Output JSON object where the parameters are added
 */
void config_add_module_params_json(const MXS_MODULE* mod,
                                   MXS_CONFIG_PARAMETER* parameters,
                                   const MXS_MODULE_PARAM* type_params,
                                   json_t* output);

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
