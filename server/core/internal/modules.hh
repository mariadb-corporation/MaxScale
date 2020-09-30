/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/dcb.hh>
#include <maxscale/modinfo.hh>

/* Module types */
#define MODULE_PROTOCOL         "Protocol"          /**< A protocol module type */
#define MODULE_AUTHENTICATOR    "Authenticator"     /**< An authenticator module type */
#define MODULE_ROUTER           "Router"            /**< A router module type */
#define MODULE_MONITOR          "Monitor"           /**< A database monitor module type */
#define MODULE_FILTER           "Filter"            /**< A filter module type */
#define MODULE_QUERY_CLASSIFIER "QueryClassifier"   /**< A query classifier module type */

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
 *@brief Load a module
 *
 * @param name Name of the module to load
 * @param type Type of module
 * @return The module specific entry point structure or NULL
 */
void* load_module(const char* name, mxs::ModuleType type);

/**
 * @brief Get a module
 *
 * @param name Name of the module
 * @param type The module type or NULL for any type
 * @return The loaded module or NULL if the module is not loaded
 */
const MXS_MODULE* get_module(const char* name, const char* type);

const MXS_MODULE* get_module(const std::string& name, mxs::ModuleType type);

/**
 * @brief Unload a module.
 *
 * No errors are returned since it is not clear that much can be done
 * to fix issues relating to unloading modules.
 *
 * @param name The name of the module
 */
void unload_module(const char* name);

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
 * @brief Print Modules to a DCB
 *
 * Diagnostic routine to display all the loaded modules
 */
void dprintAllModules(DCB*);

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

mxs::ModuleType module_type_from_string(const std::string& type_str);