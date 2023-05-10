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

#include <maxscale/paths.hh>

#include <string>
#include <maxscale/utils.hh>
#include "internal/defaults.hh"

namespace
{

using namespace mxs;

struct Directory
{
    Directory(const char* zPath)
        : path(zPath)
    {
    }

    void set(const std::string& path, config::Origin origin)
    {
        mxb_assert(origin != config::Origin::DEFAULT);

        if (static_cast<int>(origin) >= static_cast<int>(this->origin))
        {
            this->path = clean_up_pathname(path);
            this->origin = origin;
        }
    }

    std::string path;
    config::Origin origin { config::Origin::DEFAULT };
};

struct
{
    Directory   configdir           { cmake_defaults::DEFAULT_CONFIGDIR };
    Directory   config_persistdir   { cmake_defaults::DEFAULT_CONFIG_PERSISTDIR };
    Directory   module_configdir    { cmake_defaults::DEFAULT_MODULE_CONFIGDIR };
    Directory   logdir              { cmake_defaults::DEFAULT_LOGDIR };
    Directory   libdir              { cmake_defaults::DEFAULT_LIBDIR };
    Directory   sharedir            { cmake_defaults::DEFAULT_SHAREDIR };
    Directory   cachedir            { cmake_defaults::DEFAULT_CACHEDIR };
    Directory   datadir             { cmake_defaults::DEFAULT_DATADIR };
    std::string processdatadir      { cmake_defaults::DEFAULT_DATADIR };
    Directory   langdir             { cmake_defaults::DEFAULT_LANGDIR };
    Directory   piddir              { cmake_defaults::DEFAULT_PIDDIR };
    Directory   execdir             { cmake_defaults::DEFAULT_EXECDIR };
    Directory   connector_plugindir { cmake_defaults::DEFAULT_CONNECTOR_PLUGINDIR };
} this_unit;
}

namespace maxscale
{

void set_configdir(const char* path, config::Origin origin)
{
    this_unit.configdir.set(path, origin);
}

void set_module_configdir(const char* path, config::Origin origin)
{
    this_unit.module_configdir.set(path, origin);
}

void set_config_persistdir(const char* path, config::Origin origin)
{
    this_unit.config_persistdir.set(path, origin);
}

void set_logdir(const char* path, config::Origin origin)
{
    this_unit.logdir.set(path, origin);
}

void set_langdir(const char* path, config::Origin origin)
{
    this_unit.langdir.set(path, origin);
}

void set_piddir(const char* path, config::Origin origin)
{
    this_unit.piddir.set(path, origin);
}

void set_cachedir(const char* path, config::Origin origin)
{
    this_unit.cachedir.set(path, origin);
}

void set_datadir(const char* path, config::Origin origin)
{
    this_unit.datadir.set(path, origin);
}

void set_process_datadir(const char* path)
{
    this_unit.processdatadir = clean_up_pathname(path);
}

void set_libdir(const char* path, config::Origin origin)
{
    this_unit.libdir.set(path, origin);
}

void set_sharedir(const char* path, config::Origin origin)
{
    this_unit.sharedir.set(path, origin);
}

void set_execdir(const char* path, config::Origin origin)
{
    this_unit.execdir.set(path, origin);
}

void set_connector_plugindir(const char* path, config::Origin origin)
{
    this_unit.connector_plugindir.set(path, origin);
}

const char* libdir()
{
    return this_unit.libdir.path.c_str();
}

const char* sharedir()
{
    return this_unit.sharedir.path.c_str();
}

const char* cachedir()
{
    return this_unit.cachedir.path.c_str();
}

const char* datadir()
{
    return this_unit.datadir.path.c_str();
}

const char* process_datadir()
{
    return this_unit.processdatadir.c_str();
}

const char* configdir()
{
    return this_unit.configdir.path.c_str();
}

const char* module_configdir()
{
    return this_unit.module_configdir.path.c_str();
}

const char* config_persistdir()
{
    return this_unit.config_persistdir.path.c_str();
}

const char* piddir()
{
    return this_unit.piddir.path.c_str();
}

const char* logdir()
{
    return this_unit.logdir.path.c_str();
}

const char* langdir()
{
    return this_unit.langdir.path.c_str();
}

const char* execdir()
{
    return this_unit.execdir.path.c_str();
}

const char* connector_plugindir()
{
    return this_unit.connector_plugindir.path.c_str();
}
}
