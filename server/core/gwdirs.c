#include <gwdirs.h>

/**
 * Get the directory with all the modules.
 * @return The module directory
 */
char* get_libdir()
{
    return libdir?libdir:(char*)default_libdir;
}

/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_cachedir()
{
    return cachedir?cachedir:(char*)default_cachedir;
}


/**
 * Get the service cache directory
 * @return The path to the cache directory
 */
char* get_datadir()
{
    return maxscaledatadir?maxscaledatadir:(char*)default_datadir;
}
