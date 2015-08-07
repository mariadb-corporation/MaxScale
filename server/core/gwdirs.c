#include <gwdirs.h>

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