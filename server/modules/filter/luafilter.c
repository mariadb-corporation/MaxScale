/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014-2016
 */

/**
 * @file luafilter.c - Lua Filter
 *
 * A filter that calls a set of functions in a Lua script.
 *
 * The entry points for the Lua script expect the following signatures:
 *  * nil createInstance() - global script only
 *  * nil newSession()
 *  * nil closeSession()
 *  * (nil | bool | string) routeQuery(string)
 *  * nil clientReply()
 *  * string diagnostic() - global script only
 *
 * These functions, if found in the script, will be called whenever a call to the
 * matching entry point is made.
 *
 * The details for each entry point are documented in the functions.
 * @see createInstance, newSession, closeSession, routeQuery, clientReply, diagnostic
 *
 * The filter has two scripts, a global and a session script. If the global script
 * is defined and valid, the matching entry point function in Lua will be called.
 * The same holds true for session script apart from no calls to createInstance
 * or diagnostic being made for the session script.
 */

#include <skygw_types.h>
#include <spinlock.h>
#include <skygw_debug.h>
#include <log_manager.h>
#include <string.h>
#include <filter.h>
#include <session.h>
#include <modutil.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_EXPERIMENTAL,
    FILTER_VERSION,
    "Lua Filter"
};

static const char *version_str = "V1.0.0";

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
    return(char*) version_str;
}

/*
 * The filter entry points
 */
static FILTER *createInstance(char **options, FILTER_PARAMETER **);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static void setUpstream(FILTER *instance, void *fsession, UPSTREAM *upstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static int clientReply(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    setUpstream,
    routeQuery,
    clientReply,
    diagnostic,
};

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
FILTER_OBJECT *
GetModuleObject()
{
    return &MyObject;
}

static int id_pool = 0;

/**
 * Push an unique integer to the Lua state's stack
 * @param state Lua state
 * @return Always 1
 */
static int id_gen(lua_State* state)
{
    lua_pushinteger(state, atomic_add(&id_pool, 1));
    return 1;
}

/**
 * The Lua filter instance.
 */
typedef struct
{
    lua_State* global_lua_state;
    char* global_script;
    char* session_script;
    SPINLOCK lock;
} LUA_INSTANCE;

/**
 * The session structure for Lua filter.
 */
typedef struct
{
    SESSION* session;
    lua_State* lua_state;
    SPINLOCK lock;
    DOWNSTREAM down;
    UPSTREAM up;
} LUA_SESSION;

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
}

/**
 * Create a new instance of the Lua filter.
 *
 * The global script will be loaded in this function and executed once on a global
 * level before calling the createInstance function in the Lua script.
 * @param options The options for this filter
 * @param params  Filter parameters
 * @return The instance data for this new instance
 */
static FILTER *
createInstance(char **options, FILTER_PARAMETER **params)
{
    LUA_INSTANCE *my_instance;
    bool error = false;

    if ((my_instance = (LUA_INSTANCE*) calloc(1, sizeof(LUA_INSTANCE))) == NULL)
    {
        return NULL;
    }

    spinlock_init(&my_instance->lock);

    for (int i = 0; params[i] && !error; i++)
    {
        if (strcmp(params[i]->name, "global_script") == 0)
        {
            error = (my_instance->global_script = strdup(params[i]->value)) == NULL;
        }
        else if (strcmp(params[i]->name, "session_script") == 0)
        {
            error = (my_instance->session_script = strdup(params[i]->value)) == NULL;
        }
        else if (!filter_standard_parameter(params[i]->name))
        {
            MXS_ERROR("Unexpected parameter '%s'", params[i]->name);
            error = true;
        }
    }

    if (error)
    {
        free(my_instance->global_script);
        free(my_instance->session_script);
        free(my_instance);
        return NULL;
    }

    if (my_instance->global_script)
    {
        if ((my_instance->global_lua_state = luaL_newstate()))
        {
            luaL_openlibs(my_instance->global_lua_state);

            if (luaL_dofile(my_instance->global_lua_state, my_instance->global_script))
            {
                MXS_ERROR("luafilter: Failed to execute global script at '%s':%s.",
                          my_instance->global_script, lua_tostring(my_instance->global_lua_state, -1));
                free(my_instance->global_script);
                free(my_instance->session_script);
                free(my_instance);
                my_instance = NULL;
            }
            else if (my_instance->global_lua_state)
            {
                lua_getglobal(my_instance->global_lua_state, "createInstance");
                if (lua_pcall(my_instance->global_lua_state, 0, 0, 0))
                {
                    MXS_WARNING("luafilter: Failed to get global variable 'createInstance':  %s."
                                " The createInstance entry point will not be called for the global script.",
                                lua_tostring(my_instance->global_lua_state, -1));
                }
            }
        }
        else
        {
            MXS_ERROR("Unable to initialize new Lua state.");
            free(my_instance);
            my_instance = NULL;
        }
    }

    return(FILTER *) my_instance;
}

/**
 * Create a new session
 *
 * This function is called for each new client session and it is used to initialize
 * data used for the duration of the session.
 *
 * This function first loads the session script and executes in on a global level.
 * After this, the newSession function in the Lua scripts is called.
 *
 * There is a single C function exported as a global variable for the session
 * script named id_gen. The id_gen function returns an integer that is unique for
 * this service only. This function is only accessible to the session level scripts.
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static void * newSession(FILTER *instance, SESSION *session)
{
    LUA_SESSION *my_session;
    LUA_INSTANCE *my_instance = (LUA_INSTANCE*) instance;

    if ((my_session = (LUA_SESSION*) calloc(1, sizeof(LUA_SESSION))) == NULL)
    {
        return NULL;
    }

    spinlock_init(&my_session->lock);
    my_session->session = session;

    if (my_instance->session_script)
    {
        my_session->lua_state = luaL_newstate();
        luaL_openlibs(my_session->lua_state);

        if (luaL_dofile(my_session->lua_state, my_instance->session_script))
        {
            MXS_ERROR("luafilter: Failed to execute session script at '%s': %s.",
                      my_instance->session_script,
                      lua_tostring(my_session->lua_state, -1));
            lua_close(my_session->lua_state);
            free(my_session);
            my_session = NULL;
        }
        else
        {
            lua_pushcfunction(my_session->lua_state, id_gen);
            lua_setglobal(my_session->lua_state, "id_gen");

            lua_getglobal(my_session->lua_state, "newSession");
            if (lua_pcall(my_session->lua_state, 0, 0, 0))
            {
                MXS_WARNING("luafilter: Failed to get global variable 'newSession': '%s'."
                          " The newSession entry point will not be called.",
                          lua_tostring(my_session->lua_state, -1));
            }
        }
    }

    if (my_session && my_instance->global_lua_state)
    {
        spinlock_acquire(&my_instance->lock);
        lua_getglobal(my_instance->global_lua_state, "newSession");
        if (lua_pcall(my_instance->global_lua_state, 0, 0, 0))
        {
            MXS_WARNING("luafilter: Failed to get global variable 'newSession': '%s'."
                      " The newSession entry point will not be called for the global script.",
                      lua_tostring(my_instance->global_lua_state, -1));
        }
        spinlock_release(&my_instance->lock);
    }

    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * The closeSession function in the Lua scripts will be called.
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static void closeSession(FILTER *instance, void *session)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    LUA_INSTANCE *my_instance = (LUA_INSTANCE*) instance;


    if (my_session->lua_state)
    {
        spinlock_acquire(&my_session->lock);
        lua_getglobal(my_session->lua_state, "closeSession");
        if (lua_pcall(my_session->lua_state, 0, 0, 0))
        {
            MXS_WARNING("luafilter: Failed to get global variable 'closeSession': '%s'."
                        " The closeSession entry point will not be called.",
                      lua_tostring(my_session->lua_state, -1));
        }
        spinlock_release(&my_session->lock);
    }

    if (my_instance->global_lua_state)
    {
        spinlock_acquire(&my_instance->lock);
        lua_getglobal(my_instance->global_lua_state, "closeSession");
        if (lua_pcall(my_instance->global_lua_state, 0, 0, 0))
        {
            MXS_WARNING("luafilter: Failed to get global variable 'closeSession': '%s'."
                        " The closeSession entry point will not be called for the global script.",
                        lua_tostring(my_instance->global_lua_state, -1));
        }
        spinlock_release(&my_instance->lock);
    }
}

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void freeSession(FILTER *instance, void *session)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    lua_close(my_session->lua_state);
    free(my_session);
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param downstream	The downstream filter or router.
 */
static void setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    my_session->down = *downstream;
}

/**
 * Set the filter upstream
 * @param instance Filter instance
 * @param session Filter session
 * @param upstream Upstream filter
 */
static void setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    my_session->up = *upstream;
}

/**
 * The client reply entry point.
 *
 * This function calls the clientReply function of the Lua scripts.
 * @param instance Filter instance
 * @param session Filter session
 * @param queue Server response
 * @return 1 on success
 */
static int clientReply(FILTER *instance, void *session, GWBUF *queue)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    LUA_INSTANCE *my_instance = (LUA_INSTANCE *) instance;

    if (my_session->lua_state)
    {
        spinlock_acquire(&my_session->lock);
        lua_getglobal(my_session->lua_state, "clientReply");
        if (lua_pcall(my_session->lua_state, 0, 0, 0))
        {
            MXS_ERROR("luafilter: Session scope call to 'clientReply' failed: '%s'.",
                      lua_tostring(my_session->lua_state, -1));
        }
        spinlock_release(&my_session->lock);
    }
    if (my_instance->global_lua_state)
    {
        spinlock_acquire(&my_instance->lock);
        lua_getglobal(my_instance->global_lua_state, "clientReply");
        if (lua_pcall(my_instance->global_lua_state, 0, 0, 0))
        {
            MXS_ERROR("luafilter: Global scope call to 'clientReply' failed: '%s'.",
                      lua_tostring(my_session->lua_state, -1));
        }
        spinlock_release(&my_instance->lock);
    }

    return my_session->up.clientReply(my_session->up.instance,
                                      my_session->up.session, queue);
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * The Luafilter calls the routeQuery functions of both the session and the global script.
 * The query is passed as a string parameter to the routeQuery Lua function and
 * the return values of the session specific function, if any were returned,
 * are interpreted. If the first value is bool, it is interpreted as a decision
 * whether to route the query or to send an error packet to the client.
 * If it is a string, the current query is replaced with the return value and
 * the query will be routed. If nil is returned, the query is routed normally.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static int routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    LUA_INSTANCE *my_instance = (LUA_INSTANCE *) instance;
    DCB* dcb = my_session->session->client_dcb;
    char *fullquery = NULL, *ptr;
    bool route = true;
    GWBUF* forward = queue;
    int rc = 0;

    if (modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue))
    {
        fullquery = modutil_get_SQL(queue);

        if (fullquery && my_session->lua_state)
        {
            spinlock_acquire(&my_session->lock);
            lua_getglobal(my_session->lua_state, "routeQuery");
            lua_pushlstring(my_session->lua_state, fullquery, strlen(fullquery));
            if (lua_pcall(my_session->lua_state, 1, 1, 0))
            {
                MXS_ERROR("luafilter: Session scope call to 'routeQuery' failed: '%s'.",
                          lua_tostring(my_session->lua_state, -1));
            }
            else if (lua_gettop(my_session->lua_state))
            {
                if (lua_isstring(my_session->lua_state, -1))
                {
                    if (forward)
                    {
                        gwbuf_free(forward);
                    }
                    forward = modutil_create_query((char*) lua_tostring(my_session->lua_state, -1));
                }
                else if (lua_isboolean(my_session->lua_state, -1))
                {
                    route = lua_toboolean(my_session->lua_state, -1);
                }
            }
            spinlock_release(&my_session->lock);
        }

        if (my_instance->global_lua_state)
        {
            spinlock_acquire(&my_instance->lock);
            lua_getglobal(my_instance->global_lua_state, "routeQuery");
            lua_pushlstring(my_instance->global_lua_state, fullquery, strlen(fullquery));
            if (lua_pcall(my_instance->global_lua_state, 1, 0, 0))
            {
                MXS_ERROR("luafilter: Global scope call to 'routeQuery' failed: '%s'.",
                          lua_tostring(my_session->lua_state, -1));
            }
            else if (lua_gettop(my_instance->global_lua_state))
            {
                if (lua_isstring(my_instance->global_lua_state, -1))
                {
                    if (forward)
                    {
                        gwbuf_free(forward);
                    }
                    forward = modutil_create_query((char*)
                                                   lua_tostring(my_instance->global_lua_state, -1));
                }
                else if (lua_isboolean(my_instance->global_lua_state, -1))
                {
                    route = lua_toboolean(my_instance->global_lua_state, -1);
                }
            }
            spinlock_release(&my_instance->lock);
        }

        free(fullquery);
    }

    if (!route)
    {
        gwbuf_free(queue);
        GWBUF* err = modutil_create_mysql_err_msg(1, 0, 1045, "28000", "Access denied.");
        rc = dcb->func.write(dcb, err);
    }
    else
    {
        rc = my_session->down.routeQuery(my_session->down.instance,
                                         my_session->down.session, forward);
    }

    return rc;
}

/**
 * Diagnostics routine.
 *
 * This will call the matching diagnostics entry point in the Lua script. If the
 * Lua function returns a string, it will be printed to the client DCB.
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    LUA_INSTANCE *my_instance = (LUA_INSTANCE *) instance;

    if (my_instance)
    {
        if (my_instance->global_lua_state)
        {
            spinlock_acquire(&my_instance->lock);
            lua_getglobal(my_instance->global_lua_state, "diagnostic");
            if (lua_pcall(my_instance->global_lua_state, 0, 1, 0) == 0)
            {
                lua_gettop(my_instance->global_lua_state);
                if (lua_isstring(my_instance->global_lua_state, -1))
                {
                    dcb_printf(dcb, lua_tostring(my_instance->global_lua_state, -1));
                    dcb_printf(dcb, "\n");
                }
            }
            else
            {
                dcb_printf(dcb, "Global scope call to 'diagnostic' failed: '%s'.\n",
                           lua_tostring(my_instance->global_lua_state, -1));
            }
            spinlock_release(&my_instance->lock);
        }
        if (my_instance->global_script)
        {
            dcb_printf(dcb, "Global script: %s\n", my_instance->global_script);
        }
        if (my_instance->session_script)
        {
            dcb_printf(dcb, "Session script: %s\n", my_instance->session_script);
        }
    }
}
