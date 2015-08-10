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
    return libdir?libdir:(char*)default_libdir;
}

void set_libdir(char* param)
{
    if(libdir)
	free(libdir);
    libdir = param;
}
/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_cachedir()
{
    return cachedir?cachedir:(char*)default_cachedir;
}

void set_cachedir(char* param)
{
    if(cachedir)
	free(cachedir);
    cachedir = param;
}

/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_datadir()
{
    return maxscaledatadir?maxscaledatadir:(char*)default_datadir;
}

void set_datadir(char* param)
{
    if(maxscaledatadir)
	free(maxscaledatadir);
    maxscaledatadir = param;
}

char* get_configdir()
{
    return configdir?configdir:(char*)default_configdir;
}

char* get_piddir()
{
    return piddir?piddir:(char*)default_piddir;
}

char* get_logdir()
{
    return logdir?logdir:(char*)default_logdir;
}

char* get_langdir()
{
    return langdir?langdir:(char*)default_langdir;
}
