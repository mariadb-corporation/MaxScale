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

#define MXS_MODULE_NAME "cache"
#include <maxscale/filter.h>
#include "cache.h"
#include "sessioncache.h"

static char VERSION_STRING[] = "V1.0.0";

static FILTER*  createInstance(const char* zName, char** pzOptions, FILTER_PARAMETER** ppParams);
static void*    newSession(FILTER* pInstance, SESSION* pSession);
static void     closeSession(FILTER* pInstance, void* pSessionData);
static void     freeSession(FILTER* pInstance, void* pSessionData);
static void     setDownstream(FILTER* pInstance, void* pSessionData, DOWNSTREAM* pDownstream);
static void     setUpstream(FILTER* pInstance, void* pSessionData, UPSTREAM* pUpstream);
static int      routeQuery(FILTER* pInstance, void* pSessionData, GWBUF* pPacket);
static int      clientReply(FILTER* pInstance, void* pSessionData, GWBUF* pPacket);
static void     diagnostics(FILTER* pInstance, void* pSessionData, DCB* pDcb);
static uint64_t getCapabilities(void);

//
// Global symbols of the Module
//

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_IN_DEVELOPMENT,
    FILTER_VERSION,
    "A caching filter that is capable of caching and returning cached data."
};

extern "C" char *version()
{
    return VERSION_STRING;
}

/**
 * The module initialization functions, called when the module has
 * been loaded.
 */
extern "C" void ModuleInit()
{
}

/**
 * The module entry point function, called when the module is loaded.
 *
 * @return The module object.
 */
extern "C" FILTER_OBJECT *GetModuleObject()
{
    static FILTER_OBJECT object =
        {
            createInstance,
            newSession,
            closeSession,
            freeSession,
            setDownstream,
            setUpstream,
            routeQuery,
            clientReply,
            diagnostics,
            getCapabilities,
            NULL, // destroyInstance
        };

    return &object;
};

//
// API Implementation
//

/**
 * Create an instance of the cache filter for a particular service
 * within MaxScale.
 *
 * @param zName      The name of the instance (as defined in the config file).
 * @param pzOptions  The options for this filter
 * @param ppparams   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER *createInstance(const char* zName, char** pzOptions, FILTER_PARAMETER** ppParams)
{
    Cache* pCache = Cache::Create(zName, pzOptions, ppParams);

    return reinterpret_cast<FILTER*>(pCache);
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param pInstance  The cache instance data
 * @param pSession   The session itself
 *
 * @return Session specific data for this session
 */
static void *newSession(FILTER* pInstance, SESSION* pSession)
{
    Cache* pCache = reinterpret_cast<Cache*>(pInstance);
    SessionCache* pSessionCache = SessionCache::Create(pCache, pSession);

    return pSessionCache;
}

/**
 * A session has been closed.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session being closed
 */
static void closeSession(FILTER* pInstance, void* pSessionData)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    pSessionCache->close();
}

/**
 * Free the session data.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session being closed
 */
static void freeSession(FILTER* pInstance, void* pSessionData)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    delete pSessionCache;
}

/**
 * Set the downstream component for this filter.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session
 * @param pDownstream   The downstream filter or router
 */
static void setDownstream(FILTER* pInstance, void* pSessionData, DOWNSTREAM* pDownstream)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    pSessionCache->setDownstream(pDownstream);
}

/**
 * Set the upstream component for this filter.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session
 * @param pUpstream     The upstream filter or router
 */
static void setUpstream(FILTER* pInstance, void* pSessionData, UPSTREAM* pUpstream)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    pSessionCache->setUpstream(pUpstream);
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param pInstance     The filter instance data
 * @param pSessionData  The filter session data
 * @param pPacket       Buffer containing an MySQL protocol packet.
 */
static int routeQuery(FILTER* pInstance, void* pSessionData, GWBUF* pPacket)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    return pSessionCache->routeQuery(pPacket);
}

/**
 * A response on its way to the client is delivered to this function.
 *
 * @param pInstance     The filter instance data
 * @param pSessionData  The filter session data
 * @param pPacket       The response data
 */
static int clientReply(FILTER* pInstance, void* pSessionData, GWBUF* pPacket)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    return pSessionCache->clientReply(pPacket);
}

/**
 * Diagnostics routine
 *
 * If cpSessionData is NULL then print diagnostics on the instance as a whole,
 * otherwise print diagnostics for the particular session.
 *
 * @param pInstance     The filter instance
 * @param pSessionData  Filter session, may be NULL
 * @param pDcb          The DCB for diagnostic output
 */
static void diagnostics(FILTER* pInstance, void* pSessionData, DCB* pDcb)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    pSessionCache->diagnostics(pDcb);
}


/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(void)
{
    return RCAP_TYPE_TRANSACTION_TRACKING;
}
