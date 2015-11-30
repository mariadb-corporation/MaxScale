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

void set_configdir(char* str)
{
    free(configdir);
    configdir = str;
}

void set_logdir(char* str)
{
    free(logdir);
    logdir = str;
}

void set_langdir(char* str)
{
    free(langdir);
    langdir = str;
}

void set_piddir(char* str)
{
    free(piddir);
    piddir = str;
}

/**
 * Get the directory with all the modules.
 * @return The module directory
 */
char* get_libdir()
{
    return libdir ? libdir : (char*)default_libdir;
}

void set_libdir(char* param)
{
    if (libdir)
    {
        free(libdir);
    }

    libdir = param;
}
/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_cachedir()
{
    return cachedir ? cachedir : (char*)default_cachedir;
}

void set_cachedir(char* param)
{
    if (cachedir)
    {
        free(cachedir);
    }

    cachedir = param;
}

/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_datadir()
{
    return maxscaledatadir ? maxscaledatadir : (char*)default_datadir;
}

void set_datadir(char* param)
{
    if (maxscaledatadir)
    {
        free(maxscaledatadir);
    }

    maxscaledatadir = param;
}

char* get_configdir()
{
    return configdir ? configdir : (char*)default_configdir;
}

char* get_piddir()
{
    return piddir ? piddir : (char*)default_piddir;
}

char* get_logdir()
{
    return logdir ? logdir : (char*)default_logdir;
}

char* get_langdir()
{
    return langdir ? langdir : (char*)default_langdir;
}
