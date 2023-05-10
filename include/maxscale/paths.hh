#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>

namespace maxscale
{

/**
 * Set the library directory. Modules will be loaded from here.
 */
void set_libdir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the share directory
 */
void set_sharedir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the data directory
 */
void set_datadir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the process data directory
 */
void set_process_datadir(std::string_view path);

/**
 * Set the cache directory
 */
void set_cachedir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the configuration file directory
 */
void set_configdir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the configuration parts file directory
 */
void set_config_persistdir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the module configuration file directory
 */
void set_module_configdir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the log file directory
 */
void set_logdir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the language file directory
 */
void set_langdir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the PID file directory
 */
void set_piddir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the executable directory. Internal processes will look for executables
 * from here.
 */
void set_execdir(std::string_view path, config::Origin origin = config::Origin::USER);

/**
 * Set the connector plugin directory.
 */
void set_connector_plugindir(std::string_view path, config::Origin origin = config::Origin::USER);

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
