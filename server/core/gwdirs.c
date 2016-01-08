/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

#include <gwdirs.h>
#include <gw.h>

/**
 * Set the configuration file directory
 * @param str Path to directory
 */
void set_configdir(char* str)
{
    free(configdir);
    clean_up_pathname(str);
    configdir = str;
}

/**
 * Set the log file directory
 * @param str Path to directory
 */
void set_logdir(char* str)
{
    free(logdir);
    clean_up_pathname(str);
    logdir = str;
}

/**
 * Set the language file directory
 * @param str Path to directory
 */
void set_langdir(char* str)
{
    free(langdir);
    clean_up_pathname(str);
    langdir = str;
}

/**
 * Set the PID file directory
 * @param str Path to directory
 */
void set_piddir(char* str)
{
    free(piddir);
    clean_up_pathname(str);
    piddir = str;
}

/**
 * Set the cache directory
 * @param str Path to directory
 */
void set_cachedir(char* param)
{
    free(cachedir);
    clean_up_pathname(param);
    cachedir = param;
}

/**
 * Set the data directory
 * @param str Path to directory
 */
void set_datadir(char* param)
{
    free(maxscaledatadir);
    clean_up_pathname(param);
    maxscaledatadir = param;
}

/**
 * Set the library directory. Modules will be loaded from here.
 * @param str Path to directory
 */
void set_libdir(char* param)
{
    free(libdir);
    clean_up_pathname(param);
    libdir = param;
}

/**
 * Get the directory with all the modules.
 * @return The module directory
 */
char* get_libdir()
{
    return libdir ? libdir : (char*) default_libdir;
}

/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_cachedir()
{
    return cachedir ? cachedir : (char*) default_cachedir;
}

/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_datadir()
{
    return maxscaledatadir ? maxscaledatadir : (char*) default_datadir;
}

/**
 * Get the configuration file directory
 * @return The path to the configuration file directory
 */
char* get_configdir()
{
    return configdir ? configdir : (char*) default_configdir;
}

/**
 * Get the PID file directory which contains maxscale.pid
 * @return Path to the PID file directory
 */
char* get_piddir()
{
    return piddir ? piddir : (char*) default_piddir;
}

/**
 * Return the log file directory
 * @return Path to the log file directory
 */
char* get_logdir()
{
    return logdir ? logdir : (char*) default_logdir;
}

/**
 * Path to the directory which contains the errmsg.sys language file
 * @return Path to the language file directory
 */
char* get_langdir()
{
    return langdir ? langdir : (char*) default_langdir;
}
