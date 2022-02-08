/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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
 * Config section structure. Holds configuration data during startup.
 */
struct ConfigSection
{
    enum class SourceType
    {
        MAIN,       /**< Main config file, may contain [maxscale] */
        ADDITIONAL, /**< Additional config files located in the .d-directory */
        RUNTIME     /**< Runtime generated files. Can contain any section and will overwrite existing. */
    };

    ConfigSection(std::string header, SourceType source_type);
    ConfigSection(std::string header, SourceType source_type, std::string source_file, int lineno);

    const std::string m_name;                           /**< The name of the object being configured */
    const SourceType  source_type {SourceType::MAIN};   /**< Source file type */
    const std::string source_file;                      /**< Source file path */
    const int         source_lineno {-1};               /**< Source file line number */

    mxs::ConfigParameters m_parameters;     /**< The list of parameter values */

    const char* name() const
    {
        return m_name.c_str();
    }
};

using ConfigSectionMap = std::map<std::string, ConfigSection>;

/**
 * @brief Load the specified configuration file for MaxScale
 *
 * This function loads and parses the configuration file, checks for duplicate sections,
 * validates the module parameters and adds the parameter values to the context. Also loads
 * config files from user-generated directory and the runtime config directory.
 *
 * @param main_cfg_file Path to main configuration file
 * @param main_cfg_in Main config file contents, with variable substitution performed.
 * @param output   Destination object
 *
 * @return True on success, false on fatal error
 */
bool config_load_and_process(const std::string& main_cfg_file,
                             const mxb::ini::map_result::Configuration& main_cfg_in,
                             ConfigSectionMap& output);

/**
 * Apply the [maxscale]-section from the main configuration file.
 *
 * @param config Parsed main config file, after variable substitution.
 * @return True on success.
 */
bool apply_main_config(const mxb::ini::map_result::Configuration& config);

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
bool export_config_file(const char* filename, ConfigSectionMap& config);

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

/**
 * Check and add contents of config file to config context object.
 *
 * @param type Source filename. Used for log messages.
 * @param source_type Source file type
 * @param input Source object
 * @param output Destination object
 *
 * @return True if config was valid.
 */
bool config_add_to_context(const std::string& type, ConfigSection::SourceType source_type,
                           const mxb::ini::map_result::Configuration& input, ConfigSectionMap& output);

/**
 * Enable or disable masking of passwords
 *
 * @param enable If true, passwords are masked (the default state). If false, the passwords are not masked.
 *
 * @return The old value
 */
bool config_set_mask_passwords(bool enable);

/**
 * Check if passwords should be masked
 *
 * @return True if passwords should be masked.
 */
bool config_mask_passwords();
