/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file luafilter.c - Lua Filter
 *
 * A filter that calls a set of functions in a Lua script.
 *
 * The entry points for the Lua script expect the following signatures:
 *  * nil createInstance() - global script only
 *  * nil newSession(string, string)
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

#define MXS_MODULE_NAME "luafilter"

#include <maxscale/cdefs.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/debug.h>
#include <maxscale/filter.h>
#include <maxscale/log_manager.h>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>
#include <maxscale/session.h>
#include <maxscale/spinlock.h>

/*
 * The filter entry points
 */
static MXS_FILTER *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *);
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession,  MXS_DOWNSTREAM *downstream);
static void setUpstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession,  MXS_UPSTREAM *upstream);
static int32_t routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static int32_t clientReply(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static json_t* diagnostic_json(const MXS_FILTER *instance, const MXS_FILTER_SESSION *fsession);
static uint64_t getCapabilities(MXS_FILTER *instance);

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_FILTER_OBJECT MyObject =
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
        diagnostic_json,
        getCapabilities,
        NULL, // No destroyInstance
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_EXPERIMENTAL,
        MXS_FILTER_VERSION,
        "Lua Filter",
        "V1.0.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"global_script", MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
            {"session_script", MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

static int id_pool = 0;
static GWBUF *current_global_query = NULL;

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

static int lua_qc_get_type_mask(lua_State* state)
{
    int ibuf = lua_upvalueindex(1);
    GWBUF *buf = *((GWBUF**)lua_touserdata(state, ibuf));

    if (buf)
    {
        uint32_t type = qc_get_type_mask(buf);
        char *mask = qc_typemask_to_string(type);
        lua_pushstring(state, mask);
        MXS_FREE(mask);
    }
    else
    {
        lua_pushliteral(state, "");
    }

    return 1;
}

static int lua_qc_get_operation(lua_State* state)
{
    int ibuf = lua_upvalueindex(1);
    GWBUF *buf = *((GWBUF**)lua_touserdata(state, ibuf));

    if (buf)
    {
        qc_query_op_t op = qc_get_operation(buf);
        const char *opstring = qc_op_to_string(op);
        lua_pushstring(state, opstring);
    }
    else
    {
        lua_pushliteral(state, "");
    }

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
    MXS_SESSION* session;
    lua_State* lua_state;
    GWBUF* current_query;
    SPINLOCK lock;
    MXS_DOWNSTREAM down;
    MXS_UPSTREAM up;
} LUA_SESSION;

/**
 * Create a new instance of the Lua filter.
 *
 * The global script will be loaded in this function and executed once on a global
 * level before calling the createInstance function in the Lua script.
 * @param options The options for this filter
 * @param params  Filter parameters
 * @return The instance data for this new instance
 */
static MXS_FILTER *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    LUA_INSTANCE *my_instance;

    if ((my_instance = (LUA_INSTANCE*) MXS_CALLOC(1, sizeof(LUA_INSTANCE))) == NULL)
    {
        return NULL;
    }

    spinlock_init(&my_instance->lock);

    my_instance->global_script = config_copy_string(params, "global_script");
    my_instance->session_script = config_copy_string(params, "session_script");

    if (my_instance->global_script)
    {
        if ((my_instance->global_lua_state = luaL_newstate()))
        {
            luaL_openlibs(my_instance->global_lua_state);

            if (luaL_dofile(my_instance->global_lua_state, my_instance->global_script))
            {
                MXS_ERROR("Failed to execute global script at '%s':%s.",
                          my_instance->global_script, lua_tostring(my_instance->global_lua_state, -1));
                MXS_FREE(my_instance->global_script);
                MXS_FREE(my_instance->session_script);
                MXS_FREE(my_instance);
                my_instance = NULL;
            }
            else if (my_instance->global_lua_state)
            {
                lua_getglobal(my_instance->global_lua_state, "createInstance");

                if (lua_pcall(my_instance->global_lua_state, 0, 0, 0))
                {
                    MXS_WARNING("Failed to get global variable 'createInstance':  %s."
                                " The createInstance entry point will not be called for the global script.",
                                lua_tostring(my_instance->global_lua_state, -1));
                    lua_pop(my_instance->global_lua_state, -1); // Pop the error off the stack
                }

                /** Expose a part of the query classifier API */
                lua_pushlightuserdata(my_instance->global_lua_state, &current_global_query);
                lua_pushcclosure(my_instance->global_lua_state, lua_qc_get_type_mask, 1);
                lua_setglobal(my_instance->global_lua_state, "lua_qc_get_type_mask");

                lua_pushlightuserdata(my_instance->global_lua_state, &current_global_query);
                lua_pushcclosure(my_instance->global_lua_state, lua_qc_get_operation, 1);
                lua_setglobal(my_instance->global_lua_state, "lua_qc_get_operation");

            }
        }
        else
        {
            MXS_ERROR("Unable to initialize new Lua state.");
            MXS_FREE(my_instance);
            my_instance = NULL;
        }
    }

    return (MXS_FILTER *) my_instance;
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
 * @param instance The filter instance data
 * @param session The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    LUA_SESSION *my_session;
    LUA_INSTANCE *my_instance = (LUA_INSTANCE*) instance;

    if ((my_session = (LUA_SESSION*) MXS_CALLOC(1, sizeof(LUA_SESSION))) == NULL)
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
            MXS_ERROR("Failed to execute session script at '%s': %s.",
                      my_instance->session_script,
                      lua_tostring(my_session->lua_state, -1));
            lua_close(my_session->lua_state);
            MXS_FREE(my_session);
            my_session = NULL;
        }
        else
        {
            /** Expose an ID generation function */
            lua_pushcfunction(my_session->lua_state, id_gen);
            lua_setglobal(my_session->lua_state, "id_gen");

            /** Expose a part of the query classifier API */
            lua_pushlightuserdata(my_session->lua_state, &my_session->current_query);
            lua_pushcclosure(my_session->lua_state, lua_qc_get_type_mask, 1);
            lua_setglobal(my_session->lua_state, "lua_qc_get_type_mask");

            lua_pushlightuserdata(my_session->lua_state, &my_session->current_query);
            lua_pushcclosure(my_session->lua_state, lua_qc_get_operation, 1);
            lua_setglobal(my_session->lua_state, "lua_qc_get_operation");

            /** Call the newSession entry point */
            lua_getglobal(my_session->lua_state, "newSession");
            lua_pushstring(my_session->lua_state, session->client_dcb->user);
            lua_pushstring(my_session->lua_state, session->client_dcb->remote);

            if (lua_pcall(my_session->lua_state, 2, 0, 0))
            {
                MXS_WARNING("Failed to get global variable 'newSession': '%s'."
                            " The newSession entry point will not be called.",
                            lua_tostring(my_session->lua_state, -1));
                lua_pop(my_session->lua_state, -1); // Pop the error off the stack
            }
        }
    }

    if (my_session && my_instance->global_lua_state)
    {
        spinlock_acquire(&my_instance->lock);

        lua_getglobal(my_instance->global_lua_state, "newSession");
        lua_pushstring(my_instance->global_lua_state, session->client_dcb->user);
        lua_pushstring(my_instance->global_lua_state, session->client_dcb->remote);

        if (lua_pcall(my_instance->global_lua_state, 2, 0, 0))
        {
            MXS_WARNING("Failed to get global variable 'newSession': '%s'."
                        " The newSession entry point will not be called for the global script.",
                        lua_tostring(my_instance->global_lua_state, -1));
            lua_pop(my_instance->global_lua_state, -1); // Pop the error off the stack
        }

        spinlock_release(&my_instance->lock);
    }

    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * The closeSession function in the Lua scripts will be called.
 * @param instance The filter instance data
 * @param session The session being closed
 */
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    LUA_INSTANCE *my_instance = (LUA_INSTANCE*) instance;


    if (my_session->lua_state)
    {
        spinlock_acquire(&my_session->lock);

        lua_getglobal(my_session->lua_state, "closeSession");

        if (lua_pcall(my_session->lua_state, 0, 0, 0))
        {
            MXS_WARNING("Failed to get global variable 'closeSession': '%s'."
                        " The closeSession entry point will not be called.",
                        lua_tostring(my_session->lua_state, -1));
            lua_pop(my_session->lua_state, -1);
        }
        spinlock_release(&my_session->lock);
    }

    if (my_instance->global_lua_state)
    {
        spinlock_acquire(&my_instance->lock);

        lua_getglobal(my_instance->global_lua_state, "closeSession");

        if (lua_pcall(my_instance->global_lua_state, 0, 0, 0))
        {
            MXS_WARNING("Failed to get global variable 'closeSession': '%s'."
                        " The closeSession entry point will not be called for the global script.",
                        lua_tostring(my_instance->global_lua_state, -1));
            lua_pop(my_instance->global_lua_state, -1);
        }
        spinlock_release(&my_instance->lock);
    }
}

/**
 * Free the memory associated with the session
 *
 * @param instance The filter instance
 * @param session The filter session
 */
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;

    if (my_session->lua_state)
    {
        lua_close(my_session->lua_state);
    }

    MXS_FREE(my_session);
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance The filter instance data
 * @param session The filter session
 * @param downstream The downstream filter or router.
 */
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session,  MXS_DOWNSTREAM *downstream)
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
static void setUpstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session,  MXS_UPSTREAM *upstream)
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
static int32_t clientReply(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    LUA_SESSION *my_session = (LUA_SESSION *) session;
    LUA_INSTANCE *my_instance = (LUA_INSTANCE *) instance;

    if (my_session->lua_state)
    {
        spinlock_acquire(&my_session->lock);

        lua_getglobal(my_session->lua_state, "clientReply");

        if (lua_pcall(my_session->lua_state, 0, 0, 0))
        {
            MXS_ERROR("Session scope call to 'clientReply' failed: '%s'.",
                      lua_tostring(my_session->lua_state, -1));
            lua_pop(my_session->lua_state, -1);
        }

        spinlock_release(&my_session->lock);
    }
    if (my_instance->global_lua_state)
    {
        spinlock_acquire(&my_instance->lock);

        lua_getglobal(my_instance->global_lua_state, "clientReply");

        if (lua_pcall(my_instance->global_lua_state, 0, 0, 0))
        {
            MXS_ERROR("Global scope call to 'clientReply' failed: '%s'.",
                      lua_tostring(my_session->lua_state, -1));
            lua_pop(my_instance->global_lua_state, -1);
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
 * @param instance The filter instance data
 * @param session The filter session
 * @param queue  The query data
 */
static int32_t routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
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

            /** Store the current query being processed */
            my_session->current_query = queue;

            lua_getglobal(my_session->lua_state, "routeQuery");

            lua_pushlstring(my_session->lua_state, fullquery, strlen(fullquery));

            if (lua_pcall(my_session->lua_state, 1, 1, 0))
            {
                MXS_ERROR("Session scope call to 'routeQuery' failed: '%s'.",
                          lua_tostring(my_session->lua_state, -1));
                lua_pop(my_session->lua_state, -1);
            }
            else if (lua_gettop(my_session->lua_state))
            {
                if (lua_isstring(my_session->lua_state, -1))
                {
                    gwbuf_free(forward);
                    forward = modutil_create_query(lua_tostring(my_session->lua_state, -1));
                }
                else if (lua_isboolean(my_session->lua_state, -1))
                {
                    route = lua_toboolean(my_session->lua_state, -1);
                }
            }
            my_session->current_query = NULL;

            spinlock_release(&my_session->lock);
        }

        if (my_instance->global_lua_state)
        {
            spinlock_acquire(&my_instance->lock);
            current_global_query = queue;

            lua_getglobal(my_instance->global_lua_state, "routeQuery");

            lua_pushlstring(my_instance->global_lua_state, fullquery, strlen(fullquery));

            if (lua_pcall(my_instance->global_lua_state, 1, 1, 0))
            {
                MXS_ERROR("Global scope call to 'routeQuery' failed: '%s'.",
                          lua_tostring(my_instance->global_lua_state, -1));
                lua_pop(my_instance->global_lua_state, -1);
            }
            else if (lua_gettop(my_instance->global_lua_state))
            {
                if (lua_isstring(my_instance->global_lua_state, -1))
                {
                    gwbuf_free(forward);
                    forward = modutil_create_query(lua_tostring(my_instance->global_lua_state, -1));
                }
                else if (lua_isboolean(my_instance->global_lua_state, -1))
                {
                    route = lua_toboolean(my_instance->global_lua_state, -1);
                }
            }

            current_global_query = NULL;
            spinlock_release(&my_instance->lock);
        }

        MXS_FREE(fullquery);
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
 * @param instance The filter instance
 * @param fsession Filter session, may be NULL
 * @param dcb  The DCB for diagnostic output
 */
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
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
                    dcb_printf(dcb, "%s", lua_tostring(my_instance->global_lua_state, -1));
                    dcb_printf(dcb, "\n");
                }
            }
            else
            {
                dcb_printf(dcb, "Global scope call to 'diagnostic' failed: '%s'.\n",
                           lua_tostring(my_instance->global_lua_state, -1));
                lua_pop(my_instance->global_lua_state, -1);
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

/**
 * Diagnostics routine.
 *
 * This will call the matching diagnostics entry point in the Lua script. If the
 * Lua function returns a string, it will be printed to the client DCB.
 *
 * @param instance The filter instance
 * @param fsession Filter session, may be NULL
 */
static json_t* diagnostic_json(const MXS_FILTER *instance, const MXS_FILTER_SESSION *fsession)
{
    LUA_INSTANCE *my_instance = (LUA_INSTANCE *)instance;
    json_t* rval = json_object();

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
                    json_object_set_new(rval, "script_output",
                                        json_string(lua_tostring(my_instance->global_lua_state, -1)));
                }
            }
            else
            {
                lua_pop(my_instance->global_lua_state, -1);
            }
            spinlock_release(&my_instance->lock);
        }
        if (my_instance->global_script)
        {
            json_object_set_new(rval, "global_script", json_string(my_instance->global_script));
        }
        if (my_instance->session_script)
        {
            json_object_set_new(rval, "session_script", json_string(my_instance->session_script));
        }
    }

    return rval;
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER *instance)
{
    return RCAP_TYPE_NONE;
}
