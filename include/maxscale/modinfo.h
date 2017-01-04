#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file modinfo.h The module information interface
 *
 * @verbatim
 * Revision History
 *
 * Date     Who             Description
 * 02/06/14 Mark Riddoch    Initial implementation
 *
 * @endverbatim
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
    MXS_MODULE_PARAM_BOOL, /**< Boolean value */
    MXS_MODULE_PARAM_STRING, /**< String value */
    MXS_MODULE_PARAM_ENUM /**< Enumeration of string values */
};

/** Module parameter declaration */
typedef struct mxs_module_param
{
    const char *name; /**< Name of the parameter */
    enum mxs_module_param_type type; /**< Type of the parameter */
    const char *default_value;
    const char **accepted_values; /**< Only for enum values */
} MXS_MODULE_PARAM;

/**
 * The module information structure
 */
typedef struct
{
    MXS_MODULE_API      modapi;        /**< Module API type */
    MXS_MODULE_STATUS   status;        /**< Module development status */
    MXS_MODULE_VERSION  api_version;   /**< Module API version */
    const char     *description;   /**< Module description */
    const char     *version;       /**< Module version */
    void           *module_object; /**< Module type specific API implementation */
    MXS_MODULE_PARAM parameters[]; /**< Declared parameters */
} MXS_MODULE;

/**
 * This should be the last value given to @c parameters. If the module has no
 * parameters, it should be the only value.
 */
#define MXS_END_MODULE_PARAMS .name = NULL

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
 *    static FILTER_OBJECT my_object = { ... };
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
