#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file modinfo.h The module information interface
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

/**
 * The status of the module. This gives some idea of the module
 * maturity.
 */
typedef enum
{
    MXS_MODULE_IN_DEVELOPMENT = 0,
    MXS_MODULE_ALPHA_RELEASE,
    MXS_MODULE_BETA_RELEASE,
    MXS_MODULE_GA,
    MXS_MODULE_EXPERIMENTAL
} MXS_MODULE_STATUS;

/**
 * The API implemented by the module
 */
typedef enum
{
    MXS_MODULE_API_PROTOCOL = 0,
    MXS_MODULE_API_ROUTER,
    MXS_MODULE_API_MONITOR,
    MXS_MODULE_API_FILTER,
    MXS_MODULE_API_AUTHENTICATOR,
    MXS_MODULE_API_QUERY_CLASSIFIER,
} MXS_MODULE_API;

/**
 * The module version structure.
 *
 * The rules for changing these values are:
 *
 * Any change that affects an inexisting call in the API in question,
 * making the new API no longer compatible with the old,
 * must increment the major version.
 *
 * Any change that adds to the API, but does not alter the existing API
 * calls, must increment the minor version.
 *
 * Any change that is purely cosmetic and does not affect the calling
 * conventions of the API must increment only the patch version number.
 */
typedef struct
{
    int     major;
    int     minor;
    int     patch;
} MXS_MODULE_VERSION;

enum mxs_module_param_type
{
    MXS_MODULE_PARAM_COUNT, /**< Non-negative number */
    MXS_MODULE_PARAM_INT, /**< Integer number */
    MXS_MODULE_PARAM_SIZE, /**< Size in bytes */
    MXS_MODULE_PARAM_BOOL, /**< Boolean value */
    MXS_MODULE_PARAM_STRING, /**< String value */
    MXS_MODULE_PARAM_ENUM, /**< Enumeration of string values */
    MXS_MODULE_PARAM_PATH, /**< Path to a file or a directory */
    MXS_MODULE_PARAM_SERVICE, /**< Service name */
    MXS_MODULE_PARAM_SERVER, /**< Server name */
};

/** Maximum and minimum values for integer types */
#define MXS_MODULE_PARAM_COUNT_MAX "2147483647"
#define MXS_MODULE_PARAM_COUNT_MIN "0"
#define MXS_MODULE_PARAM_INT_MAX "2147483647"
#define MXS_MODULE_PARAM_INT_MIN "-2147483647"

/** Parameter options
 *
 * If no type is specified, the option can be used with all parameter types
 */
enum mxs_module_param_options
{
    MXS_MODULE_OPT_NONE        = 0,
    MXS_MODULE_OPT_REQUIRED    = (1 << 0), /**< A required parameter */
    MXS_MODULE_OPT_PATH_X_OK   = (1 << 1), /**< PATH: Execute permission to path required */
    MXS_MODULE_OPT_PATH_R_OK   = (1 << 2), /**< PATH: Read permission to path required */
    MXS_MODULE_OPT_PATH_W_OK   = (1 << 3), /**< PATH: Write permission to path required */
    MXS_MODULE_OPT_PATH_F_OK   = (1 << 4), /**< PATH: Path must exist */
    MXS_MODULE_OPT_ENUM_UNIQUE = (1 << 5)  /**< ENUM: Only one value can be defined */
};

/** String to enum value mappings */
typedef struct mxs_enum_value
{
    const char *name; /**< Name of the enum value */
    uint64_t    enum_value; /**< The integer value of the enum */
} MXS_ENUM_VALUE;

/** Module parameter declaration */
typedef struct mxs_module_param
{
    const char *name; /**< Name of the parameter */
    enum mxs_module_param_type type; /**< Type of the parameter */
    const char *default_value; /**< Default value for the parameter, NULL for no default value */
    uint64_t options; /**< Parameter options */
    const MXS_ENUM_VALUE *accepted_values; /**< Only for enum values */
} MXS_MODULE_PARAM;

/** Maximum number of parameters that modules can declare */
#define MXS_MODULE_PARAM_MAX 64

/**
 * The module information structure
 */
typedef struct mxs_module
{
    MXS_MODULE_API      modapi;        /**< Module API type */
    MXS_MODULE_STATUS   status;        /**< Module development status */
    MXS_MODULE_VERSION  api_version;   /**< Module API version */
    const char         *description;   /**< Module description */
    const char         *version;       /**< Module version */
    void               *module_object; /**< Module type specific API implementation */
    /**
     * If non-NULL, this function is called once at process startup. If the
     * function fails, MariaDB MaxScale will not start.
     *
     * @return 0 on success, non-zero on failure.
     */
    int (*process_init)();

    /**
     * If non-NULL, this function is called once at process shutdown, provided
     * the call to @c init succeeded.
     */
    void (*process_finish)();

    /**
     * If non-NULL, this function is called once at the startup of every new thread.
     * If the function fails, then the thread will terminate.
     *
     * @attention This function is *not* called for the thread where @c init is called.
     *
     * @return 0 on success, non-zero on failure.
     */
    int (*thread_init)();

    /**
     * If non-NULL, this function is called when a thread terminates, provided the
     * call to @c thread_init succeeded.
     *
     * @attention This function is *not* called for the thread where @c init is called.
     */
    void (*thread_finish)();

    MXS_MODULE_PARAM parameters[MXS_MODULE_PARAM_MAX + 1];  /**< Declared parameters */
} MXS_MODULE;

/**
 * This should be the last value given to @c parameters. If the module has no
 * parameters, it should be the only value.
 */
#define MXS_END_MODULE_PARAMS 0

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

MXS_END_DECLS
