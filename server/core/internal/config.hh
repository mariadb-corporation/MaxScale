/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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

#include <maxbase/jansson.hh>
#include <maxbase/ini.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/ssl.hh>

#define DEFAULT_QUERY_RETRIES               1       /**< Number of retries for interrupted queries */
#define DEFAULT_QUERY_RETRY_TIMEOUT         5       /**< Timeout for query retries */
#define DEFAULT_MAX_AUTH_ERRORS_UNTIL_BLOCK 10      /**< Max allowed authentication failures */

/** Object type specific parameter lists */
extern const char* config_pre_parse_global_params[];

struct SniffResult
{
    bool                                success {false};
    mxb::ini::map_result::Configuration config;
    std::string                         warning;
    std::vector<std::string>            errors;
};

bool handle_path_arg(std::string* dest, const char* path,
                     const char* arg = nullptr, const char* arg2 = nullptr);

/**
 * Sniffs the configuration file, primarily for various directory paths, so that certain settings
 * take effect immediately.
 *
 * @param filepath The path of the configuration file.
 * @return Result object
 */
SniffResult sniff_configuration(const std::string& filepath);

/**
 * Sniffs the configuration, primarily for various directory paths, so that certain settings
 * take effect immediately.
 *
 * @param config_text The config in a string.
 * @return Result object
 */
SniffResult sniff_configuration_text(const std::string& config_text);

/**
 * Config section structure. Holds configuration data during startup.
 */
struct ConfigSection
{
    enum class SourceType
    {
        MAIN       = 0, /**< Main config file, may contain [maxscale] */
        ADDITIONAL = 1, /**< Additional config files located in the .d-directory */
        RUNTIME    = 2  /**< Runtime generated files. Can contain any section and will overwrite existing. */
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
 * Calls mxb::ini::parse_config_file_to_map and handles a possible case-insensitively labeled [maxscale]-
 * section.
 *
 * @param config_file Config file to load
 * @return Tuple of actual parse results and a possible warning message regarding the maxscale-section.
 */
std::tuple<mxb::ini::map_result::ParseResult, std::string>
parse_mxs_config_file_to_map(const std::string& config_file);

/**
 * Calls mxb::ini::parse_config_text_to_map and handles a possible case-insensitively labeled [maxscale]-
 * section.
 *
 * @param config_file Config file to load
 * @return Tuple of actual parse results and a possible warning message regarding the maxscale-section.
 */
std::tuple<mxb::ini::map_result::ParseResult, std::string>
parse_mxs_config_text_to_map(const std::string& config_text);

/**
 * @brief Load the specified configuration file for MaxScale
 *
 * This function loads and parses the configuration file, checks for duplicate sections,
 * validates the module parameters but does not create any object. Also loads
 * config files from user-generated directory and the runtime config directory.
 *
 * @param main_cfg_file Path to main configuration file
 * @param main_cfg_in Main config file contents, with variable substitution performed.
 * @param output   Destination object
 *
 * @return True on success, false on fatal error
 */
bool config_load(const std::string& main_cfg_file,
                 const mxb::ini::map_result::Configuration& main_cfg_in,
                 ConfigSectionMap& output);

/**
 * @brief Process a loaded configuration.
 *
 * @param input A configuration populated by @c config_load.
 *
 * @return True on success, false on fatal error
 */
bool config_process(ConfigSectionMap& input);

/**
 * Apply the [maxscale]-section from the main configuration file.
 *
 * @param config Parsed main config file, after variable substitution.
 * @return True on success.
 */
bool apply_main_config(const ConfigSectionMap& config);

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
 * Unmasks passwords when constructed, restores the old value when the object is destroyed. Use this in
 * code that uses the JSON generated from a mxs::config::Configuration to configure something else.
 */
class UnmaskPasswords
{
public:
    UnmaskPasswords(const UnmaskPasswords&) = delete;
    UnmaskPasswords& operator=(const UnmaskPasswords&) = delete;

    UnmaskPasswords();

    ~UnmaskPasswords();

private:
    bool m_old_val;

    static std::recursive_mutex s_guard;
};

/**
 * Check if passwords should be masked
 *
 * @return True if passwords should be masked.
 */
bool config_mask_passwords();
