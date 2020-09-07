/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
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

#include <maxscale/ccdefs.hh>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <string.h>

#include <mutex>
#include <maxbase/alloc.h>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/session.hh>

/*
 * The filter entry points
 */
static MXS_FILTER*         createInstance(const char* name, mxs::ConfigParameters*);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance,
                                      MXS_SESSION* session,
                                      SERVICE* service,
                                      mxs::Downstream* downstream,
                                      mxs::Upstream* upstream);
static void    closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void    freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static int32_t routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static int32_t clientReply(MXS_FILTER* instance,
                           MXS_FILTER_SESSION* fsession,
                           GWBUF* queue,
                           const mxs::ReplyRoute& down,
                           const mxs::Reply& reply);
static json_t*  diagnostics(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);

extern "C"
{

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
        routeQuery,
        clientReply,
        diagnostics,
        getCapabilities,
        NULL,       // No destroyInstance
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
        NULL,                       /* Process init. */
        NULL,                       /* Process finish. */
        NULL,                       /* Thread init. */
        NULL,                       /* Thread finish. */
        {
            {"global_script",      MXS_MODULE_PARAM_PATH,  NULL, MXS_MODULE_OPT_PATH_R_OK},
            {"session_script",     MXS_MODULE_PARAM_PATH,  NULL, MXS_MODULE_OPT_PATH_R_OK},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

static int id_pool = 0;
static GWBUF* current_global_query = NULL;

class LUA_SESSION;
class LUA_INSTANCE;

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
    GWBUF* buf = *((GWBUF**)lua_touserdata(state, ibuf));

    if (buf)
    {
        uint32_t type = qc_get_type_mask(buf);
        char* mask = qc_typemask_to_string(type);
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
    GWBUF* buf = *((GWBUF**)lua_touserdata(state, ibuf));

    if (buf)
    {
        qc_query_op_t op = qc_get_operation(buf);
        const char* opstring = qc_op_to_string(op);
        lua_pushstring(state, opstring);
    }
    else
    {
        lua_pushliteral(state, "");
    }

    return 1;
}

static int lua_get_canonical(lua_State* state)
{
    int ibuf = lua_upvalueindex(1);
    GWBUF* buf = *((GWBUF**)lua_touserdata(state, ibuf));

    if (buf)
    {
        char* canon = modutil_get_canonical(buf);
        lua_pushstring(state, canon);
        MXS_FREE(canon);
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
class LUA_INSTANCE
{
public:
    bool configure(mxs::ConfigParameters* params);

    LUA_SESSION* newSession(MXS_SESSION* session, SERVICE* service,
                            mxs::Downstream* downstream, mxs::Upstream* upstream);

    json_t*    diagnostics() const;
    uint64_t   getCapabilities() const;
    lua_State* global_lua_state();

    mutable std::mutex m_lock;

private:
    lua_State*  m_global_lua_state {nullptr};
    std::string m_global_script;
    std::string m_session_script;
};

/**
 * The session structure for Lua filter.
 */
class LUA_SESSION
{
public:
    LUA_SESSION(MXS_SESSION* session, SERVICE* service, LUA_INSTANCE* filter);
    ~LUA_SESSION();
    void close();
    bool prepare_session(const std::string& session_script, mxs::Downstream* downstream,
                         mxs::Upstream* upstream);

    int routeQuery(GWBUF* queue);
    int clientReply(GWBUF* queue, const mxs::ReplyRoute& down, const mxs::Reply& reply);

private:
    LUA_INSTANCE*    m_filter {nullptr};
    MXS_SESSION*     m_session {nullptr};
    SERVICE*         m_service {nullptr};
    lua_State*       m_lua_state {nullptr};
    GWBUF*           m_current_query {nullptr};
    mxs::Downstream* m_down {nullptr};
    mxs::Upstream*   m_up {nullptr};
};

LUA_SESSION::LUA_SESSION(MXS_SESSION* session, SERVICE* service, LUA_INSTANCE* filter)
    : m_filter(filter)
    , m_session(session)
    , m_service(service)
{
}

void expose_functions(lua_State* state, GWBUF** active_buffer)
{
    /** Expose an ID generation function */
    lua_pushcfunction(state, id_gen);
    lua_setglobal(state, "id_gen");

    /** Expose a part of the query classifier API */
    lua_pushlightuserdata(state, active_buffer);
    lua_pushcclosure(state, lua_qc_get_type_mask, 1);
    lua_setglobal(state, "lua_qc_get_type_mask");

    lua_pushlightuserdata(state, active_buffer);
    lua_pushcclosure(state, lua_qc_get_operation, 1);
    lua_setglobal(state, "lua_qc_get_operation");

    lua_pushlightuserdata(state, active_buffer);
    lua_pushcclosure(state, lua_get_canonical, 1);
    lua_setglobal(state, "lua_get_canonical");
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
static MXS_FILTER* createInstance(const char* name, mxs::ConfigParameters* params)
{
    auto my_instance = new LUA_INSTANCE();
    if (!my_instance->configure(params))
    {
        delete my_instance;
        my_instance = nullptr;
    }
    return (MXS_FILTER*) my_instance;
}

bool LUA_INSTANCE::configure(mxs::ConfigParameters* params)
{
    bool error = false;
    m_global_script = params->get_string("global_script");
    m_session_script = params->get_string("session_script");

    if (!m_global_script.empty())
    {
        if ((m_global_lua_state = luaL_newstate()))
        {
            luaL_openlibs(m_global_lua_state);

            if (luaL_dofile(m_global_lua_state, m_global_script.c_str()))
            {
                MXS_ERROR("Failed to execute global script at '%s':%s.",
                          m_global_script.c_str(), lua_tostring(m_global_lua_state, -1));
                error = true;
            }
            else if (m_global_lua_state)
            {
                lua_getglobal(m_global_lua_state, "createInstance");

                if (lua_pcall(m_global_lua_state, 0, 0, 0))
                {
                    MXS_WARNING("Failed to get global variable 'createInstance':  %s. The createInstance "
                                "entry point will not be called for the global script.",
                                lua_tostring(m_global_lua_state, -1));
                    lua_pop(m_global_lua_state, -1);        // Pop the error off the stack
                }

                expose_functions(m_global_lua_state, &current_global_query);
            }
        }
        else
        {
            MXS_ERROR("Unable to initialize new Lua state.");
            error = true;
        }
    }
    return !error;
}

uint64_t LUA_INSTANCE::getCapabilities() const
{
    return RCAP_TYPE_NONE;
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
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance,
                                      MXS_SESSION* session,
                                      SERVICE* service,
                                      mxs::Downstream* downstream,
                                      mxs::Upstream* upstream)
{
    auto my_instance = (LUA_INSTANCE*)instance;
    return (MXS_FILTER_SESSION*)my_instance->newSession(session, service, downstream, upstream);
}

LUA_SESSION* LUA_INSTANCE::newSession(MXS_SESSION* session, SERVICE* service, mxs::Downstream* downstream,
                                      mxs::Upstream* upstream)
{
    auto new_session = new LUA_SESSION(session, service, this);
    if (!new_session->prepare_session(m_session_script, downstream, upstream))
    {
        delete new_session;
        new_session = nullptr;
    }

    if (new_session && m_global_lua_state)
    {
        std::lock_guard<std::mutex> guard(m_lock);

        lua_getglobal(m_global_lua_state, "newSession");
        lua_pushstring(m_global_lua_state, session->user().c_str());
        lua_pushstring(m_global_lua_state, session->client_remote().c_str());

        if (lua_pcall(m_global_lua_state, 2, 0, 0))
        {
            MXS_WARNING("Failed to get global variable 'newSession': '%s'."
                        " The newSession entry point will not be called for the global script.",
                        lua_tostring(m_global_lua_state, -1));
            lua_pop(m_global_lua_state, -1);        // Pop the error off the stack
        }
    }
    return new_session;
}

bool LUA_SESSION::prepare_session(const std::string& session_script, mxs::Downstream* downstream,
                                  mxs::Upstream* upstream)
{
    bool error = false;
    m_down = downstream;
    m_up = upstream;

    if (!session_script.empty())
    {
        m_lua_state = luaL_newstate();
        luaL_openlibs(m_lua_state);

        if (luaL_dofile(m_lua_state, session_script.c_str()))
        {
            MXS_ERROR("Failed to execute session script at '%s': %s.",
                      session_script.c_str(),
                      lua_tostring(m_lua_state, -1));
            lua_close(m_lua_state);
            error = true;
        }
        else
        {
            expose_functions(m_lua_state, &m_current_query);

            /** Call the newSession entry point */
            lua_getglobal(m_lua_state, "newSession");
            lua_pushstring(m_lua_state, m_session->user().c_str());
            lua_pushstring(m_lua_state, m_session->client_remote().c_str());

            if (lua_pcall(m_lua_state, 2, 0, 0))
            {
                MXS_WARNING("Failed to get global variable 'newSession': '%s'. The newSession entry "
                            "point will not be called.",
                            lua_tostring(m_lua_state, -1));
                lua_pop(m_lua_state, -1);       // Pop the error off the stack
            }
        }
    }
    return !error;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * The closeSession function in the Lua scripts will be called.
 * @param instance The filter instance data
 * @param session The session being closed
 */
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    auto my_session = (LUA_SESSION*)session;
    my_session->close();
}

void LUA_SESSION::close()
{
    if (m_lua_state)
    {
        lua_getglobal(m_lua_state, "closeSession");
        if (lua_pcall(m_lua_state, 0, 0, 0))
        {
            MXS_WARNING("Failed to get global variable 'closeSession': '%s'. The closeSession entry point "
                        "will not be called.",
                        lua_tostring(m_lua_state, -1));
            lua_pop(m_lua_state, -1);
        }
    }

    auto global_lua_state = m_filter->global_lua_state();
    if (global_lua_state)
    {
        std::lock_guard<std::mutex> guard(m_filter->m_lock);

        lua_getglobal(global_lua_state, "closeSession");
        if (lua_pcall(global_lua_state, 0, 0, 0))
        {
            MXS_WARNING("Failed to get global variable 'closeSession': '%s'. The closeSession entry point "
                        "will not be called for the global script.",
                        lua_tostring(global_lua_state, -1));
            lua_pop(global_lua_state, -1);
        }
    }
}

/**
 * Free the memory associated with the session
 *
 * @param instance The filter instance
 * @param session The filter session
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    auto my_session = (LUA_SESSION*) session;
    delete my_session;
}

LUA_SESSION::~LUA_SESSION()
{
    if (m_lua_state)
    {
        lua_close(m_lua_state);
    }
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
static int32_t clientReply(MXS_FILTER* instance,
                           MXS_FILTER_SESSION* session,
                           GWBUF* queue,
                           const mxs::ReplyRoute& down,
                           const mxs::Reply& reply)
{
    auto my_session = (LUA_SESSION*) session;
    return my_session->clientReply(queue, down, reply);
}

int LUA_SESSION::clientReply(GWBUF* queue, const maxscale::ReplyRoute& down, const mxs::Reply& reply)
{
    LUA_SESSION* my_session = this;

    if (my_session->m_lua_state)
    {
        lua_getglobal(my_session->m_lua_state, "clientReply");

        if (lua_pcall(my_session->m_lua_state, 0, 0, 0))
        {
            MXS_ERROR("Session scope call to 'clientReply' failed: '%s'.", lua_tostring(m_lua_state, -1));
            lua_pop(my_session->m_lua_state, -1);
        }
    }

    auto global_lua_state = m_filter->global_lua_state();
    if (global_lua_state)
    {
        std::lock_guard<std::mutex> guard(m_filter->m_lock);

        lua_getglobal(global_lua_state, "clientReply");

        if (lua_pcall(global_lua_state, 0, 0, 0))
        {
            MXS_ERROR("Global scope call to 'clientReply' failed: '%s'.", lua_tostring(m_lua_state, -1));
            lua_pop(global_lua_state, -1);
        }
    }

    return m_up->clientReply(m_up->instance, m_up->session, queue, down, reply);
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
static int32_t routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    auto my_session = (LUA_SESSION*) session;
    return my_session->routeQuery(queue);
}

int LUA_SESSION::routeQuery(GWBUF* queue)
{
    auto my_session = this;
    char* fullquery = NULL;
    bool route = true;
    GWBUF* forward = queue;
    int rc = 0;

    if (modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue))
    {
        fullquery = modutil_get_SQL(queue);

        if (fullquery && my_session->m_lua_state)
        {
            /** Store the current query being processed */
            my_session->m_current_query = queue;

            lua_getglobal(my_session->m_lua_state, "routeQuery");

            lua_pushlstring(my_session->m_lua_state, fullquery, strlen(fullquery));

            if (lua_pcall(my_session->m_lua_state, 1, 1, 0))
            {
                MXS_ERROR("Session scope call to 'routeQuery' failed: '%s'.",
                          lua_tostring(my_session->m_lua_state, -1));
                lua_pop(my_session->m_lua_state, -1);
            }
            else if (lua_gettop(my_session->m_lua_state))
            {
                if (lua_isstring(my_session->m_lua_state, -1))
                {
                    gwbuf_free(forward);
                    forward = modutil_create_query(lua_tostring(my_session->m_lua_state, -1));
                }
                else if (lua_isboolean(my_session->m_lua_state, -1))
                {
                    route = lua_toboolean(my_session->m_lua_state, -1);
                }
            }
            my_session->m_current_query = NULL;
        }

        auto global_lua_state = m_filter->global_lua_state();
        if (global_lua_state)
        {
            std::lock_guard<std::mutex> guard(m_filter->m_lock);
            current_global_query = queue;

            lua_getglobal(global_lua_state, "routeQuery");

            lua_pushlstring(global_lua_state, fullquery, strlen(fullquery));

            if (lua_pcall(global_lua_state, 1, 1, 0))
            {
                MXS_ERROR("Global scope call to 'routeQuery' failed: '%s'.",
                          lua_tostring(global_lua_state, -1));
                lua_pop(global_lua_state, -1);
            }
            else if (lua_gettop(global_lua_state))
            {
                if (lua_isstring(global_lua_state, -1))
                {
                    gwbuf_free(forward);
                    forward = modutil_create_query(lua_tostring(global_lua_state, -1));
                }
                else if (lua_isboolean(global_lua_state, -1))
                {
                    route = lua_toboolean(global_lua_state, -1);
                }
            }

            current_global_query = NULL;
        }

        MXS_FREE(fullquery);
    }

    if (!route)
    {
        gwbuf_free(queue);
        GWBUF* err = modutil_create_mysql_err_msg(1, 0, 1045, "28000", "Access denied.");
        session_set_response(m_session, m_service, m_up, err);
        rc = 1;
    }
    else
    {
        rc = m_down->routeQuery(m_down->instance, m_down->session, forward);
    }

    return rc;
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
static json_t* diagnostics(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    auto my_instance = (LUA_INSTANCE*)instance;
    return my_instance->diagnostics();
}

json_t* LUA_INSTANCE::diagnostics() const
{
    json_t* rval = json_object();
    if (m_global_lua_state)
    {
        std::lock_guard<std::mutex> guard(m_lock);

        lua_getglobal(m_global_lua_state, "diagnostic");

        if (lua_pcall(m_global_lua_state, 0, 1, 0) == 0)
        {
            lua_gettop(m_global_lua_state);
            if (lua_isstring(m_global_lua_state, -1))
            {
                json_object_set_new(rval, "script_output", json_string(lua_tostring(m_global_lua_state, -1)));
            }
        }
        else
        {
            lua_pop(m_global_lua_state, -1);
        }
    }
    if (!m_global_script.empty())
    {
        json_object_set_new(rval, "global_script", json_string(m_global_script.c_str()));
    }
    if (!m_session_script.empty())
    {
        json_object_set_new(rval, "session_script", json_string(m_session_script.c_str()));
    }
    return rval;
}

lua_State* LUA_INSTANCE::global_lua_state()
{
    return m_global_lua_state;
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    auto my_instance = (LUA_INSTANCE*)instance;
    return my_instance->getCapabilities();
}
