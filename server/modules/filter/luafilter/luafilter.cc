/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <string.h>
#include <mutex>
#include <maxbase/alloc.h>
#include <maxscale/config_common.hh>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/session.hh>
#include <maxsimd/canonical.hh>

namespace
{

namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamPath s_global_script(
    &s_spec, "global_script", "Path to global level Lua script",
    cfg::ParamPath::R, "");

cfg::ParamPath s_session_script(
    &s_spec, "session", "Path to session level Lua script",
    cfg::ParamPath::R, "");
}

static int id_pool = 0;
static GWBUF* current_global_query = NULL;

class LuaFilterSession;
class LuaFilter;

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
        std::string sql = mxs::extract_sql(buf);
        maxsimd::Markers markers;
        maxsimd::get_canonical(&sql, &markers);
        lua_pushstring(state, sql.c_str());
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
class LuaFilter : public mxs::Filter
{
public:

    class Config : public mxs::config::Configuration
    {
    public:
        Config(LuaFilter* instance, const char* name);

        std::string global_script;
        std::string session_script;

    protected:
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

        LuaFilter* m_instance;
    };

    static LuaFilter* create(const char* name);

    mxs::FilterSession* newSession(MXS_SESSION* session, SERVICE* service);
    ~LuaFilter();

    json_t*  diagnostics() const;
    uint64_t getCapabilities() const;

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

    lua_State* global_lua_state();

    bool post_configure();

    mutable std::mutex m_lock;

private:

    LuaFilter(const char* name)
        : m_config(this, name)
    {
    }

    lua_State* m_global_lua_state {nullptr};
    Config     m_config;
};

/**
 * The session structure for Lua filter.
 */
class LuaFilterSession : public maxscale::FilterSession
{
public:
    LuaFilterSession(MXS_SESSION* session, SERVICE* service, LuaFilter* filter);
    ~LuaFilterSession();
    bool prepare_session(const std::string& session_script);

    int routeQuery(GWBUF* queue);
    int clientReply(GWBUF* queue, const mxs::ReplyRoute& down, const mxs::Reply& reply);

private:
    LuaFilter*   m_filter {nullptr};
    MXS_SESSION* m_session {nullptr};
    SERVICE*     m_service {nullptr};
    lua_State*   m_lua_state {nullptr};
    GWBUF*       m_current_query {nullptr};
};

LuaFilterSession::LuaFilterSession(MXS_SESSION* session, SERVICE* service, LuaFilter* filter)
    : FilterSession(session, service)
    , m_filter(filter)
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

LuaFilter::Config::Config(LuaFilter* instance, const char* name)
    : mxs::config::Configuration(name, &s_spec)
    , m_instance(instance)
{
    add_native(&Config::global_script, &s_global_script);
    add_native(&Config::session_script, &s_session_script);
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
LuaFilter* LuaFilter::create(const char* name)
{
    return new LuaFilter(name);
}

bool LuaFilter::Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    return m_instance->post_configure();
}

bool LuaFilter::post_configure()
{
    bool error = false;

    if (!m_config.global_script.empty())
    {
        if ((m_global_lua_state = luaL_newstate()))
        {
            luaL_openlibs(m_global_lua_state);

            if (luaL_dofile(m_global_lua_state, m_config.global_script.c_str()))
            {
                MXS_ERROR("Failed to execute global script at '%s':%s.",
                          m_config.global_script.c_str(), lua_tostring(m_global_lua_state, -1));
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

uint64_t LuaFilter::getCapabilities() const
{
    return RCAP_TYPE_NONE;
}

mxs::FilterSession* LuaFilter::newSession(MXS_SESSION* session, SERVICE* service)
{
    auto new_session = new LuaFilterSession(session, service, this);
    if (!new_session->prepare_session(m_config.session_script))
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

bool LuaFilterSession::prepare_session(const std::string& session_script)
{
    bool error = false;
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

LuaFilterSession::~LuaFilterSession()
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

        lua_close(m_lua_state);
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

int LuaFilterSession::clientReply(GWBUF* queue, const maxscale::ReplyRoute& down, const mxs::Reply& reply)
{
    LuaFilterSession* my_session = this;

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

    return FilterSession::clientReply(queue, down, reply);
}

int LuaFilterSession::routeQuery(GWBUF* queue)
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
        FilterSession::set_response(err);
        rc = 1;
    }
    else
    {
        rc = FilterSession::routeQuery(forward);
    }

    return rc;
}

json_t* LuaFilter::diagnostics() const
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
    if (!m_config.global_script.empty())
    {
        json_object_set_new(rval, "global_script", json_string(m_config.global_script.c_str()));
    }
    if (!m_config.session_script.empty())
    {
        json_object_set_new(rval, "session_script", json_string(m_config.session_script.c_str()));
    }
    return rval;
}

lua_State* LuaFilter::global_lua_state()
{
    return m_global_lua_state;
}

LuaFilter::~LuaFilter()
{
    if (m_global_lua_state)
    {
        lua_close(m_global_lua_state);
    }
}

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
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::EXPERIMENTAL,
        MXS_FILTER_VERSION,
        "Lua Filter",
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::FilterApi<LuaFilter>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {
            {MXS_END_MODULE_PARAMS}
        },
        &s_spec
    };

    return &info;
}
}
