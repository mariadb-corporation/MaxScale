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
    MODULE_IN_DEVELOPMENT = 0,
    MODULE_ALPHA_RELEASE,
    MODULE_BETA_RELEASE,
    MODULE_GA,
    MODULE_EXPERIMENTAL
} MODULE_STATUS;

/**
 * The API implemented by the module
 */
typedef enum
{
    MODULE_API_PROTOCOL = 0,
    MODULE_API_ROUTER,
    MODULE_API_MONITOR,
    MODULE_API_FILTER,
    MODULE_API_AUTHENTICATOR,
    MODULE_API_QUERY_CLASSIFIER,
} MODULE_API;

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
} MODULE_VERSION;

/**
 * The module information structure
 */
typedef struct
{
    MODULE_API      modapi;        /**< Module API type */
    MODULE_STATUS   status;        /**< Module development status */
    MODULE_VERSION  api_version;   /**< Module API version */
    const char     *description;   /**< Module description */
    const char     *version;       /**< Module version */
    void           *module_object; /**< Module type specific API implementation */
} MODULE_INFO;

/**
 * Name of the module entry point
 *
 * All modules should declare the module entry point in the following style:
 *
 * @code{.cpp}
 *
 * MODULE_INFO* MXS_CREATE_MODULE()
 * {
 *     // Module specific API implementation
 *    static FILTER_OBJECT my_object = { ... };
 *
 *     // An implementation of the MODULE_INFO structure
 *    static MODULE_INFO info = { ... };
 *
 *     // Any global initialization should be done here
 *
 *     return &info;
 * }
 *
 * @endcode
 *
 * The @c module_object field of the MODULE_INFO structure should point to
 * the module type specific API implementation. In the above example, the @c info
 * would declare a pointer to @c my_object as the last member of the struct.
 */
#define MXS_CREATE_MODULE mxs_get_module_object

/** Name of the symbol that MaxScale will load */
#define MXS_MODULE_SYMBOL_NAME "mxs_get_module_object"

MXS_END_DECLS
