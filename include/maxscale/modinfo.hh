/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file modinfo.hh The module information interface
 */

#include <maxscale/ccdefs.hh>
#include <stdint.h>
#include <maxbase/assert.h>
#include <maxscale/version.h>

namespace maxscale
{
namespace config
{
class Specification;
}

/**
 * The status of the module. This gives some idea of the module maturity.
 */
enum class ModuleStatus
{
    IN_DEVELOPMENT = 0,
    ALPHA,
    BETA,
    GA,
    EXPERIMENTAL
};
/**
 * The API implemented by the module. "Unknown" is not a valid value for a module, but is used by the loader
 * when loading an unknown module type.
 */
enum class ModuleType
{
    UNKNOWN = 0,
    PROTOCOL,
    ROUTER,
    MONITOR,
    FILTER,
    AUTHENTICATOR,
    QUERY_CLASSIFIER,
};
}

/**
 * The module version structure.
 *
 * The rules for changing these values are:
 *
 * Any change that affects an existing call in the API,
 * making the new API no longer compatible with the old,
 * must increment the major version.
 *
 * Any change that adds to the API, but does not alter the existing API
 * calls, must increment the minor version.
 *
 * Any change that is purely cosmetic and does not affect the calling
 * conventions of the API must increment only the patch version number.
 */
struct MXS_MODULE_VERSION
{
    int major {0};
    int minor {0};
    int patch {0};

    bool operator==(const MXS_MODULE_VERSION& rhs) const;
};

enum mxs_module_param_type
{
    MXS_MODULE_PARAM_COUNT,         /**< Non-negative number */
    MXS_MODULE_PARAM_INT,           /**< Integer number */
    MXS_MODULE_PARAM_SIZE,          /**< Size in bytes */
    MXS_MODULE_PARAM_BOOL,          /**< Boolean value */
    MXS_MODULE_PARAM_STRING,        /**< String value */
    MXS_MODULE_PARAM_QUOTEDSTRING,  /**< String enclosed in '"':s */
    MXS_MODULE_PARAM_PASSWORD,      /**< Password value that is masked in all output  */
    MXS_MODULE_PARAM_ENUM,          /**< Enumeration of string values */
    MXS_MODULE_PARAM_PATH,          /**< Path to a file or a directory */
    MXS_MODULE_PARAM_SERVICE,       /**< Service name */
    MXS_MODULE_PARAM_SERVER,        /**< Server name */
    MXS_MODULE_PARAM_TARGET,        /**< Target name (server or service) */
    MXS_MODULE_PARAM_SERVERLIST,    /**< List of server names, separated by ',' */
    MXS_MODULE_PARAM_TARGETLIST,    /**< List of target names, separated by ',' */
    MXS_MODULE_PARAM_REGEX,         /**< A regex string enclosed in '/' */
    MXS_MODULE_PARAM_DURATION,      /**< Duration in milliseconds */
    MXS_MODULE_PARAM_DEPRECATED,    /**< Deprecated value (only here until the legacy system is removed) */
};

/** Maximum and minimum values for integer types */
#define MXS_MODULE_PARAM_COUNT_MAX "2147483647"
#define MXS_MODULE_PARAM_COUNT_MIN "0"
#define MXS_MODULE_PARAM_INT_MAX   "2147483647"
#define MXS_MODULE_PARAM_INT_MIN   "-2147483647"

/** Parameter options
 *
 * If no type is specified, the option can be used with all parameter types
 */
enum mxs_module_param_options
{
    MXS_MODULE_OPT_NONE        = 0,
    MXS_MODULE_OPT_REQUIRED    = (1 << 0),  /**< A required parameter */
    MXS_MODULE_OPT_PATH_X_OK   = (1 << 1),  /**< PATH: Execute permission to path required */
    MXS_MODULE_OPT_PATH_R_OK   = (1 << 2),  /**< PATH: Read permission to path required */
    MXS_MODULE_OPT_PATH_W_OK   = (1 << 3),  /**< PATH: Write permission to path required */
    MXS_MODULE_OPT_PATH_F_OK   = (1 << 4),  /**< PATH: Path must exist */
    MXS_MODULE_OPT_PATH_CREAT  = (1 << 5),  /**< PATH: Create path if it doesn't exist */
    MXS_MODULE_OPT_ENUM_UNIQUE = (1 << 6),  /**< ENUM: Only one value can be defined */
    MXS_MODULE_OPT_DURATION_S  = (1 << 7),  /**< DURATION: Cannot be specified in milliseconds */
    MXS_MODULE_OPT_DEPRECATED  = (1 << 8),  /**< Parameter is deprecated: Causes a warning to be logged if the
                                             * parameter is used but will not cause a configuration error. */
};

/** String to enum value mappings */
struct MXS_ENUM_VALUE
{
    const char* name;       /**< Name of the enum value */
    uint64_t    enum_value; /**< The integer value of the enum */
};

/** Module parameter declaration */
struct MXS_MODULE_PARAM
{
    const char*           name;             /**< Name of the parameter */
    mxs_module_param_type type;             /**< Type of the parameter */
    const char*           default_value;    /**< Default value for the parameter, NULL for no default value */
    uint64_t              options;          /**< Parameter options */
    const MXS_ENUM_VALUE* accepted_values;  /**< Only for enum values */
};

/** Maximum number of parameters that modules can declare */
#define MXS_MODULE_PARAM_MAX 64

namespace maxscale
{
constexpr uint32_t MODULE_INFO_VERSION = 10000 * MAXSCALE_VERSION_MAJOR + 100 * MAXSCALE_VERSION_MINOR
    + MAXSCALE_VERSION_PATCH;
}

/**
 * The module information structure
 */
struct MXS_MODULE
{
    /**
     * MaxScale version number the struct was created in. Should match MaxScale version to avoid loading
     * modules from previous versions.
     * */
    uint32_t           mxs_version;
    const char*        name;                /**< Module name */
    mxs::ModuleType    modapi;              /**< Module API type */
    mxs::ModuleStatus  status;              /**< Module development status */
    MXS_MODULE_VERSION api_version;         /**< Module API version */
    const char*        description;         /**< Module description */
    const char*        version;             /**< Module version */
    uint64_t           module_capabilities; /**< Declared module capabilities */
    void*              module_object;       /**< Module type specific API implementation */

    /**
     * If non-NULL, this function is called once at process startup. If the
     * function fails, MariaDB MaxScale will not start.
     *
     * @return 0 on success, non-zero on failure.
     */
    int (* process_init)();

    /**
     * If non-NULL, this function is called once at process shutdown, provided
     * the call to @c init succeeded.
     */
    void (* process_finish)();

    /**
     * If non-NULL, this function is called once at the startup of every new thread.
     * If the function fails, then the thread will terminate.
     *
     * @attention This function is *not* called for the thread where @c init is called.
     *
     * @return 0 on success, non-zero on failure.
     */
    int (* thread_init)();

    /**
     * If non-NULL, this function is called when a thread terminates, provided the
     * call to @c thread_init succeeded.
     *
     * @attention This function is *not* called for the thread where @c init is called.
     */
    void (* thread_finish)();

    MXS_MODULE_PARAM parameters[MXS_MODULE_PARAM_MAX + 1];      /**< Declared parameters */

    const mxs::config::Specification* specification;        /**< Configuration specification */
};

/**
 * This should be the last value given to @c parameters. If the module has no
 * parameters, it should be the only value.
 */
#define MXS_END_MODULE_PARAMS 0

/**
 * This value should be given to the @c module_capabilities member if the module
 * declares no capabilities. Currently only routers and filters can declare
 * capabilities.
 */
#define MXS_NO_MODULE_CAPABILITIES 0

/**
 * Name of the module entry point
 *
 * All modules should declare the module entry point in the following style:
 *
 * @code{.cpp}
 *
 * MXS_MODULE* MXS_CREATE_MODULE()
 * {
 *     // Module specific API implementation
 *    static MXS_FILTER_OBJECT my_object = { ... };
 *
 *     // An implementation of the MXS_MODULE structure
 *    static MXS_MODULE info = { ... };
 *
 *     // Any global initialization should be done here
 *
 *     return &info;
 * }
 *
 * @endcode
 *
 * The @c module_object field of the MODULE structure should point to
 * the module type specific API implementation. In the above example, the @c info
 * would declare a pointer to @c my_object as the last member of the struct.
 */
#define MXS_CREATE_MODULE mxs_get_module_object

/** Name of the symbol that MaxScale will load */
#define MXS_MODULE_SYMBOL_NAME "mxs_get_module_object"
