/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/paths.hh>

#include <string>
#include <maxscale/utils.hh>
#include "internal/defaults.hh"

namespace
{
struct
{
    std::string configdir = cmake_defaults::DEFAULT_CONFIGDIR;
    std::string config_persistdir = cmake_defaults::DEFAULT_CONFIG_PERSISTDIR;
    std::string module_configdir = cmake_defaults::DEFAULT_MODULE_CONFIGDIR;
    std::string logdir = cmake_defaults::DEFAULT_LOGDIR;
    std::string libdir = cmake_defaults::DEFAULT_LIBDIR;
    std::string sharedir = cmake_defaults::DEFAULT_SHAREDIR;
    std::string cachedir = cmake_defaults::DEFAULT_CACHEDIR;
    std::string datadir = cmake_defaults::DEFAULT_DATADIR;
    std::string processdatadir = cmake_defaults::DEFAULT_DATADIR;
    std::string langdir = cmake_defaults::DEFAULT_LANGDIR;
    std::string piddir = cmake_defaults::DEFAULT_PIDDIR;
    std::string execdir = cmake_defaults::DEFAULT_EXECDIR;
    std::string connector_plugindir = cmake_defaults::DEFAULT_CONNECTOR_PLUGINDIR;
} this_unit;
}

namespace maxscale
{

void set_configdir(const char* path)
{
    this_unit.configdir = clean_up_pathname(path);
}

void set_module_configdir(const char* path)
{
    this_unit.module_configdir = clean_up_pathname(path);
}

void set_config_persistdir(const char* path)
{
    this_unit.config_persistdir = clean_up_pathname(path);
}

void set_logdir(const char* path)
{
    this_unit.logdir = clean_up_pathname(path);
}

void set_langdir(const char* path)
{
    this_unit.langdir = clean_up_pathname(path);
}

void set_piddir(const char* path)
{
    this_unit.piddir = clean_up_pathname(path);
}

void set_cachedir(const char* path)
{
    this_unit.cachedir = clean_up_pathname(path);
}

void set_datadir(const char* path)
{
    this_unit.datadir = clean_up_pathname(path);
}

void set_process_datadir(const char* path)
{
    this_unit.processdatadir = clean_up_pathname(path);
}

void set_libdir(const char* path)
{
    this_unit.libdir = clean_up_pathname(path);
}

void set_sharedir(const char* path)
{
    this_unit.sharedir = clean_up_pathname(path);
}

void set_execdir(const char* path)
{
    this_unit.execdir = clean_up_pathname(path);
}

void set_connector_plugindir(const char* path)
{
    this_unit.connector_plugindir = clean_up_pathname(path);
}

const char* libdir()
{
    return this_unit.libdir.c_str();
}

const char* sharedir()
{
    return this_unit.sharedir.c_str();
}

const char* cachedir()
{
    return this_unit.cachedir.c_str();
}

const char* datadir()
{
    return this_unit.datadir.c_str();
}

const char* process_datadir()
{
    return this_unit.processdatadir.c_str();
}

const char* configdir()
{
    return this_unit.configdir.c_str();
}

const char* module_configdir()
{
    return this_unit.module_configdir.c_str();
}

const char* config_persistdir()
{
    return this_unit.config_persistdir.c_str();
}

const char* piddir()
{
    return this_unit.piddir.c_str();
}

const char* logdir()
{
    return this_unit.logdir.c_str();
}

const char* langdir()
{
    return this_unit.langdir.c_str();
}

const char* execdir()
{
    return this_unit.execdir.c_str();
}

const char* connector_plugindir()
{
    return this_unit.connector_plugindir.c_str();
}
}
