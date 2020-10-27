/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/modinfo.hh>

#define MODULE_FILTER           "Filter"
#define MODULE_QUERY_CLASSIFIER "QueryClassifier"

/**
 * Calls init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
bool modules_process_init();

/**
 * Calls process_finish on all loaded modules.
 */
void modules_process_finish();

/**
 * Calls thread_init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
bool modules_thread_init();

/**
 * Calls thread_finish on all loaded modules.
 */
void modules_thread_finish();

/**
 * Loads all modules
 *
 * @return True if all modules were loaded successfully
 */
bool load_all_modules();

/**
 * @brief Get a module
 *
 * @param name Name of the module
 * @param type The module type. If UNKNOWN, any type is accepted.
 * @return The loaded module or NULL if the module was not loaded
 */
const MXS_MODULE* get_module(const std::string& name, mxs::ModuleType type);

/**
 * @brief Unload all modules
 *
 * Remove all the modules from the system, called during shutdown
 * to allow termination hooks to be called.
 */
void unload_all_modules();

/**
 * @brief Print Modules
 *
 * Diagnostic routine to display all the loaded modules
 */
void printModules();

/**
 * @brief Convert module to JSON
 *
 * @param module Module to convert
 * @param host   Hostname of this server
 *
 * @return The module in JSON format
 */
json_t* module_to_json(const MXS_MODULE* module, const char* host);

/**
 * @brief Convert all modules to JSON
 *
 * @param host The hostname of this server
 *
 * @return Array of modules in JSON format
 */
json_t* module_list_to_json(const char* host);

/**
 * @brief Return effective module name.
 *
 * The effective module name is the actual name of a module. In case
 * a module has been renamed (and the old name deprecated), the effective
 * name of a module may be different from the used one.
 *
 * @param name  The name of a module.
 *
 * @return The effective name (may be the same).
 */
std::string module_get_effective_name(const std::string& name);

/**
 * @brief Convert configuration specification to a MaxScale module in JSON format
 *
 * @param host Hostname of this server
 * @param spec The configuration specification to convert
 *
 * @return The specification as a MaxScale module in JSON format
 */
json_t* spec_module_to_json(const char* host, const mxs::config::Specification& spec);

/**
 * Add a compile-time linked module. Should only be called from the main executable during startup.
 *
 * @param module Module info object
 */
void add_built_in_module(MXS_MODULE* module);
