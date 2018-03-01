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
 * @file include/maxscale/filter.h - The public filter interface
 */

#include <maxscale/cdefs.h>
#include <stdint.h>
#include <maxscale/buffer.h>
#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/routing.h>
#include <maxscale/session.h>

MXS_BEGIN_DECLS

/**
 * MXS_FILTER is an opaque type representing a particular filter instance.
 *
 * MaxScale itself does not do anything with it, except for receiving it
 * from the @c createInstance function of a filter module and subsequently
 * passing it back to the API functions of the filter.
 */
typedef struct mxs_filter
{
} MXS_FILTER;

/**
 * MXS_FILTER_SESSION is an opaque type representing the session related
 * data of a particular filter instance.
 *
 * MaxScale itself does not do anything with it, except for receiving it
 * from the @c newSession function of a filter module and subsequently
 * passing it back to the API functions of the filter.
 */
typedef struct mxs_filter_session
{
} MXS_FILTER_SESSION;

/**
 * @verbatim
 * The "module object" structure for a filter module
 *
 * The entry points are:
 *      createInstance   Called by the service to create a new instance of the filter
 *      newSession       Called to create a new user session within the filter
 *      closeSession     Called when a session is closed
 *      freeSession      Called when a session is freed
 *      setDownstream    Sets the downstream component of the filter pipline
 *      setUpstream      Sets the upstream component of the filter pipline
 *      routeQuery       Called on each query that requires routing
 *      clientReply      Called for each reply packet
 *      diagnostics      Called for diagnostic output
 *      getCapabilities  Called to obtain the capabilities of the filter
 *      destroyInstance  Called for destroying a filter instance
 *
 * @endverbatim
 *
 * @see load_module
 */
typedef struct mxs_filter_object
{

    /**
     * @brief Create a new instance of the filter
     *
     * This function is called when a new filter instance is created. The return
     * value of this function will be passed as the first parameter to the
     * other API functions.
     *
     * @param name    Name of the filter instance
     * @param options Filter options
     * @param params  Filter parameters
     *
     * @return New filter instance on NULL on error
     */
    MXS_FILTER *(*createInstance)(const char *name, char **options, MXS_CONFIG_PARAMETER *params);

    /**
     * Called to create a new user session within the filter
     *
     * This function is called when a new filter session is created for a client.
     * The return value of this function will be passed as the second parameter
     * to the @c routeQuery, @c clientReply, @c closeSession, @c freeSession,
     * @c setDownstream and @c setUpstream functions.
     *
     * @param instance Filter instance
     * @param session Client SESSION object
     *
     * @return New filter session or NULL on error
     */
    MXS_FILTER_SESSION *(*newSession)(MXS_FILTER *instance, MXS_SESSION *session);

    /**
     * @brief Called when a session is closed
     *
     * The filter should close all objects but not free any memory.
     *
     * @param instance Filter instance
     * @param fsession Filter session
     */
    void     (*closeSession)(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession);

    /**
     * @brief Called when a session is freed
     *
     * The session should free all allocated memory in this function.
     *
     * @param instance Filter instance
     * @param fsession Filter session
     */
    void     (*freeSession)(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession);

    /**
     * @brief Sets the downstream component of the filter pipeline
     *
     * @param instance Filter instance
     * @param fsession Filter session
     */
    void     (*setDownstream)(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);

    /**
     * @brief Sets the upstream component of the filter pipeline
     *
     * @param instance Filter instance
     * @param fsession Filter session
     */
    void     (*setUpstream)(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_UPSTREAM *downstream);

    /**
     * @brief Called on each query that requires routing
     *
     * TODO: Document how routeQuery should be used
     *
     * @param instance Filter instance
     * @param fsession Filter session
     * @param queue    Request from the client
     *
     * @return If successful, the function returns 1. If an error occurs
     * and the session should be closed, the function returns 0.
     */
    int32_t  (*routeQuery)(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);

    /**
     * @brief Called for each reply packet
     *
     * TODO: Document how clientReply should be used
     *
     * @param instance Filter instance
     * @param fsession Filter session
     * @param queue    Response from the server
     *
     * @return If successful, the function returns 1. If an error occurs
     * and the session should be closed, the function returns 0.
     */
    int32_t  (*clientReply)(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);

    /**
     * @brief Called for diagnostic output
     *
     * @param instance Filter instance
     * @param fsession Filter session, NULL if general information about the filter is queried
     * @param dcb      DCB where the diagnostic information should be written
     */
    void     (*diagnostics)(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);

    /**
     * @brief Called to obtain the capabilities of the filter
     *
     * @return Zero or more bitwise-or'd values from the mxs_routing_capability_t enum
     *
     * @see routing.h
     */
    uint64_t (*getCapabilities)(MXS_FILTER *instance);

    /**
     * @brief Called for destroying a filter instance
     *
     * @param instance Filter instance
     */
    void     (*destroyInstance)(MXS_FILTER *instance);

} MXS_FILTER_OBJECT;

/**
 * The filter API version. If the MXS_FILTER_OBJECT structure or the filter API
 * is changed these values must be updated in line with the rules in the
 * file modinfo.h.
 */
#define MXS_FILTER_VERSION  {2, 2, 0}

/**
 * MXS_FILTER_DEF represents a filter definition from the configuration file.
 * Its exact definition is private to MaxScale.
 */
struct mxs_filter_def;
typedef struct mxs_filter_def MXS_FILTER_DEF;

/**
 * Lookup a filter definition using the unique section name in
 * the configuration file.
 *
 * @param name The name of a filter.
 *
 * @return A filter definition or NULL if not found.
 */
MXS_FILTER_DEF *filter_def_find(const char *name);

/**
 * Get the name of a filter definition. This corresponds to
 * to a filter section in the configuration file.
 *
 * @param filter_def  A filter definition.
 *
 * @return The filter name.
 */
const char* filter_def_get_name(const MXS_FILTER_DEF* filter_def);

/**
 * Get module name of a filter definition.
 *
 * @param filter_def  A filter definition.
 *
 * @return The module name.
 */
const char* filter_def_get_module_name(const MXS_FILTER_DEF* filter_def);

/**
 * Get the filter instance of a particular filter definition.
 *
 * @return A filter instance.
 */
MXS_FILTER* filter_def_get_instance(const MXS_FILTER_DEF* filter_def);

void dprintAllFilters(DCB *);
void dprintFilter(DCB *, const MXS_FILTER_DEF *);
void dListFilters(DCB *);

/**
 * Specifies capabilities specific for filters. Common capabilities
 * are defined by @c routing_capability_t.
 *
 * @see routing_capability_t
 *
 * @note The values of the capabilities here *must* be between 0x000100000000
 *       and 0x800000000000, that is, bits 32 to 47.
 */

/*
typedef enum filter_capability
{
} filter_capability_t;
*/

MXS_END_DECLS
