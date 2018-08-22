/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

/**
 * @file modules.h  Utilities for loading modules
 */

#include <maxscale/cdefs.h>
#include <maxscale/dcb.h>
#include <maxscale/modinfo.h>

MXS_BEGIN_DECLS

/**
 * Module types
 */
#define MODULE_PROTOCOL         "Protocol"        /**< A protocol module type */
#define MODULE_AUTHENTICATOR    "Authenticator"   /**< An authenticator module type */
#define MODULE_ROUTER           "Router"          /**< A router module type */
#define MODULE_MONITOR          "Monitor"         /**< A database monitor module type */
#define MODULE_FILTER           "Filter"          /**< A filter module type */
#define MODULE_QUERY_CLASSIFIER "QueryClassifier" /**< A query classifier module type */


/**
 *@brief Load a module
 *
 * @param module Name of the module to load
 * @param type   Type of module, used purely for registration
 * @return       The module specific entry point structure or NULL
 */
void *load_module(const char *module, const char *type);

/**
 * @brief Get a module
 *
 * @param name Name of the module
 * @param type The module type or NULL for any type
 * @return The loaded module or NULL if the module is not loaded
 */
const MXS_MODULE *get_module(const char *name, const char *type);

/**
 * @brief Unload a module.
 *
 * No errors are returned since it is not clear that much can be done
 * to fix issues relating to unloading modules.
 *
 * @param module The name of the module
 */
void unload_module(const char *module);

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
void dprintAllModules(DCB *);

typedef struct mxs_module_iterator
{
    const char* type;
    void* position;
} MXS_MODULE_ITERATOR;

/**
 * @brief Returns an iterator to modules.
 *
 * @attention It is unspecified whether a module loaded after the iterator
 *            was created, will be returned by the iterator. The behaviour
 *            is undefined if a module is unloaded while an iteration is
 *            being performed.
 *
 * @param type  The type of modules that should be returned. If NULL,
 *              then all modules are returned.
 *
 * @return An iterator.
 */
MXS_MODULE_ITERATOR mxs_module_iterator_get(const char* type);

/**
 * @brief Indicates whether the iterator has a module to return.
 *
 * @param iterator  An iterator
 *
 * @return True if a subsequent call to @c mxs_module_iterator_get
 *         will return a module.
 */
bool mxs_module_iterator_has_next(const MXS_MODULE_ITERATOR* iterator);

/**
 * @brief Returns the next module and advances the iterator.
 *
 * @param iterator  An iterator.
 *
 * @return A module if there was a module to return, NULL otherwise.
 */
MXS_MODULE* mxs_module_iterator_get_next(MXS_MODULE_ITERATOR* iterator);

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
const char* mxs_module_get_effective_name(const char* name);

MXS_END_DECLS
