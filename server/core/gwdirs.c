#include <gwdirs.h>

/**
 * Get the directory with all the modules.
 * @return The module directory
 */
char* get_libdir()
{
    return libdir?libdir:(char*)default_libdir;
}
