#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

namespace maxscale
{
/**
 * Set the library directory. Modules will be loaded from here.
 */
void set_libdir(const char* param);

/**
 * Set the share directory
 */
void set_sharedir(const char* param);

/**
 * Set the data directory
 */
void set_datadir(const char* param);

/**
 * Set the process data directory
 */
void set_process_datadir(const char* param);

/**
 * Set the cache directory
 */
void set_cachedir(const char* param);

/**
 * Set the configuration file directory
 */
void set_configdir(const char* param);

/**
 * Set the configuration parts file directory
 */
void set_config_persistdir(const char* param);

/**
 * Set the module configuration file directory
 */
void set_module_configdir(const char* param);

/**
 * Set the log file directory
 */
void set_logdir(const char* param);

/**
 * Set the language file directory
 */
void set_langdir(const char* param);

/**
 * Set the PID file directory
 */
void set_piddir(const char* param);

/**
 * Set the executable directory. Internal processes will look for executables
 * from here.
 */
void set_execdir(const char* param);

/**
 * Set the connector plugin directory.
 */
void set_connector_plugindir(const char* param);

/**
 * Get the directory with all the modules.
 */
const char* libdir();

/**
 * Get the share directory
 */
const char* sharedir();

/**
 * Get the MaxScale data directory
 */
const char* datadir();

/**
 * Get the process specific data directory
 */
const char* process_datadir();

/**
 * Get the service cache directory
 */
const char* cachedir();

/**
 * Get the configuration file directory
 */
const char* configdir();

/**
 * Get the configuration file directory
 */
const char* config_persistdir();

/**
 * Get the module configuration file directory
 */
const char* module_configdir();

/**
 * Get the PID file directory which contains maxscale.pid
 */
const char* piddir();

/**
 * Return the log file directory
 */
const char* logdir();

/**
 * Path to the directory which contains the errmsg.sys language file
 */
const char* langdir();

/**
 * Get the directory with the executables.
 */
const char* execdir();

/**
 * Get connector plugin directory
 */
const char* connector_plugindir();
}
