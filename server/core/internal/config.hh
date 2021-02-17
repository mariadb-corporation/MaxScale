/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file core/maxscale/config.h - The private config interface
 */

#include <maxscale/config.hh>

#include <sstream>
#include <initializer_list>
#include <unordered_set>

#include <maxbase/jansson.h>
#include <maxscale/cn_strings.hh>
#include <maxscale/ssl.hh>

#define DEFAULT_NTHREADS                    1       /**< Default number of polling threads */
#define DEFAULT_QUERY_RETRIES               1       /**< Number of retries for interrupted queries */
#define DEFAULT_QUERY_RETRY_TIMEOUT         5       /**< Timeout for query retries */
#define MIN_WRITEQ_HIGH_WATER               4096UL  /**< Min high water mark of dcb write queue */
#define MIN_WRITEQ_LOW_WATER                512UL   /**< Min low water mark of dcb write queue */
#define DEFAULT_MAX_AUTH_ERRORS_UNTIL_BLOCK 10      /**< Max allowed authentication failures */

/**
 * Maximum length for configuration parameter value.
 */
enum
{
    MAX_PARAM_LEN = 256
};

/** Object type specific parameter lists */
extern const MXS_MODULE_PARAM config_filter_params[];
extern const char* config_pre_parse_global_params[];

/**
 * Finalize the configuration subsystem
 */
void config_finish();

/**
 * @brief Add default parameters for a module to the configuration context
 *
 * Only parameters that aren't yet in the destination container are added.
 * This allows users to override the default values.
 *
 * @param dest Container where the default parameters are added
 * @param params Module parameter definitions
 */
void config_add_defaults(mxs::ConfigParameters* dest, const MXS_MODULE_PARAM* params);

char* config_clean_string_list(const char* str);
bool  config_load(const char*);
bool  config_load_global(const char* filename);

/**
 * @brief Creates an empty configuration context
 *
 * @param section Context name
 * @return New context or NULL on memory allocation failure
 */
CONFIG_CONTEXT* config_context_create(const char* section);

/**
 * @brief Free a configuration context
 *
 * @param context The context to free
 */
void config_context_free(CONFIG_CONTEXT* context);

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
 * @brief Add non-standard configuration parameters to a JSON object
 *
 * @param parameters List of configuration parameter values
 * @param param_info Configuration parameter type information
 * @param ignored_params Set of parameters which should not be added to the output
 * @param output Output JSON object where the parameters are added
 */
void config_add_module_params_json(const mxs::ConfigParameters* parameters,
                                   const std::unordered_set<std::string>& ignored_params,
                                   const MXS_MODULE_PARAM* basic_params,
                                   const MXS_MODULE_PARAM* module_params,
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
void fix_object_name(char* name);
void fix_object_name(std::string& name);

/**
 * Export the configuration to a file
 *
 * @param filename Filename where the configuration will be written
 *
 * @return True if configuration was successfully exported
 */
bool export_config_file(const char* filename);

/**
 * Generate configuration file contents out of module configuration parameters. Only parameters defined
 * in the parameter definition arrays are printed. Printing is in the order the parameters are given in
 * the definitions.
 *
 * @param instance_name The module instance name
 * @param parameters Configuration parameter values
 * @param common_param_defs Common module parameter definitions. These are printed first.
 * @param module_param_defs Module-specific parameter definitions.
 */
std::string generate_config_string(const std::string& instance_name, const mxs::ConfigParameters& parameters,
                                   const MXS_MODULE_PARAM* common_param_defs,
                                   const MXS_MODULE_PARAM* module_param_defs);

/**
 * Check whether a parameter can be modified at runtime
 *
 * @param name Name of the parameter
 *
 * @return True if the parameter can be modified at runtime
 */
bool config_can_modify_at_runtime(const char* name);

// Value returned for unknown enumeration values
constexpr int64_t MXS_UNKNOWN_ENUM_VALUE {-1};

/**
 * Convert enum name to integer value
 *
 * @param key    The enum name to convert
 * @param values The list of enum values
 *
 * @return The enum value or MXS_UNKNOWN_ENUM_VALUE on unknown value
 */
int64_t config_enum_to_value(const std::string& key, const MXS_ENUM_VALUE* values);

bool validate_param(const MXS_MODULE_PARAM* basic, const MXS_MODULE_PARAM* module,
                    const std::string& key, const std::string& value, std::string* error_out);

bool param_is_known(const MXS_MODULE_PARAM* basic, const MXS_MODULE_PARAM* module, const char* key);

bool param_is_valid(const MXS_MODULE_PARAM* basic, const MXS_MODULE_PARAM* module,
                    const char* key, const char* value);

/**
 * Set value for 'rebalance_threshold'
 *
 * @param value  New value, expected to be 0 <= value <= 100.
 *
 * @return True, if the value was valid, false otherwise.
 */
bool config_set_rebalance_threshold(const char* value);

/**
 * @brief Check if required parameters are missing
 *
 * @param name Module name
 * @param type Module type
 * @param params List of parameters for the object
 * @return True if at least one of the required parameters is missing
 */
bool missing_required_parameters(const MXS_MODULE_PARAM* mod_params,
                                 const mxs::ConfigParameters& params,
                                 const char* name);

typedef struct duplicate_context
{
    std::set<std::string>* sections;
    pcre2_code*            re;
    pcre2_match_data*      mdata;
} DUPLICATE_CONTEXT;

/**
 * Initialize the context object used for tracking duplicate sections.
 *
 * @param context The context object to be initialized.
 *
 * @return True, if the object could be initialized.
 */
bool duplicate_context_init(DUPLICATE_CONTEXT* context);

/**
 * Finalize the context object used for tracking duplicate sections.
 *
 * @param context The context object to be initialized.
 */
void duplicate_context_finish(DUPLICATE_CONTEXT* context);

/**
 * Load single configuration file.
 *
 * @param file     The file to load.
 * @param dcontext The context object used when tracking duplicate sections.
 * @param ccontext The context object used when parsing.
 *
 * @return True if the file could be parsed, false otherwise.
 */
bool config_load_single_file(const char* file,
                             DUPLICATE_CONTEXT* dcontext,
                             CONFIG_CONTEXT* ccontext);

/**
 * Turn parameters into json.
 *
 * @return Parameters as json.
 */
json_t* config_core_params_to_json(const char* host);
