/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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
#include <maxscale/version.hh>

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

    const mxs::config::Specification* specification;        /**< Configuration specification */
};

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
