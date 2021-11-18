/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
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
#include <maxbase/ini.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/ssl.hh>

#define DEFAULT_QUERY_RETRIES               1       /**< Number of retries for interrupted queries */
#define DEFAULT_QUERY_RETRY_TIMEOUT         5       /**< Timeout for query retries */
#define DEFAULT_MAX_AUTH_ERRORS_UNTIL_BLOCK 10      /**< Max allowed authentication failures */

/** Object type specific parameter lists */
extern const char* config_pre_parse_global_params[];

/**
 * The config context structure. Holds configuration data during startup.
 */
class CONFIG_CONTEXT
{
public:
    CONFIG_CONTEXT(std::string section = "");

    std::string           m_name;           /**< The name of the object being configured */
    mxs::ConfigParameters m_parameters;     /**< The list of parameter values */

    enum class SourceType
    {
        STATIC,
        RUNTIME
    };

    SourceType  source_type {SourceType::STATIC};   /**< Source file type */
    std::string source_file;                        /**< Source file path */

    const char* name() const
    {
        return m_name.c_str();
    }
};

using ConfContextMap = std::map<std::string, CONFIG_CONTEXT>;

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

/**
 * @brief Load the specified configuration file for MaxScale
 *
 * This function loads and parses the configuration file, checks for duplicate sections,
 * validates the module parameters and adds the parameter values to the context. Also loads
 * config files from user-generated directory and the runtime config directory.
 *
 * @param main_cfg_filepath Path to main configuration file
 * @param cfg_file_contents Main config file contents, with variable substitution performed.
 * @param config_cntx_out   Destination object
 *
 * @return True on success, false on fatal error
 */
bool config_load_and_process(const std::string& main_cfg_filepath,
                             const mxb::ini::map_result::Configuration& cfg_file_contents,
                             ConfContextMap& config_cntx_out);

/**
 * Apply the [maxscale]-section from the main configuration file.
 *
 * @param config Parsed main config file, after variable substitution.
 * @return True on success.
 */
bool apply_main_config(const mxb::ini::map_result::Configuration& config);

/**
 * @brief Creates an empty configuration context
 *
 * @param section Context name
 * @return New context or NULL on memory allocation failure
 */
CONFIG_CONTEXT* config_context_create(const char* section);

/**
 * @brief Add a parameter to a configuration context
 *
 * @param obj Context where the parameter should be added
 * @param key Key to add
 * @param value Value for the key
 */
void config_add_param(CONFIG_CONTEXT* obj, const char* key, const char* value);

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
void config_add_module_params_json(const mxs::ConfigParameters& parameters,
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
bool export_config_file(const char* filename, ConfContextMap& config);

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
 * Serializes parameters into key-value pairs
 *
 * @param parameter Parameters to serialize
 * @param defs      Parameter definitions
 *
 * @return The parameters as key-value pairs delimited by newlines
 */
std::string serialize_params(const mxs::ConfigParameters& parameters, const MXS_MODULE_PARAM* defs);

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

/**
 * Check and add contents of config file to config context object.
 *
 * @param source_file Source filename. Used for log messages.
 * @param source_type Source file type
 * @param input Source object
 * @param output Destination object
 *
 * @return True if config was valid.
 */
bool config_add_to_context(const std::string& source_file, CONFIG_CONTEXT::SourceType source_type,
                           const mxb::ini::map_result::Configuration& input, ConfContextMap& output);

/**
 * Enable or disable masking of passwords
 *
 * @param enable If true, passwords are masked (the default state). If false, the passwords are not masked.
 */
void config_set_mask_passwords(bool enable);

/**
 * Check if passwords should be masked
 *
 * @return True if passwords should be masked.
 */
bool config_mask_passwords();

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
                           const ConfContextMap* context);
