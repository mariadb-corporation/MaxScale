/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "luacontext.hh"

#include <maxsimd/canonical.hh>
#include <maxscale/modutil.hh>

namespace
{

const LuaData& get_data(lua_State* state)
{
    return *static_cast<LuaData*>(lua_touserdata(state, lua_upvalueindex(1)));
}

static int lua_get_session_id(lua_State* state)
{
    uint64_t id = 0;
    const auto& data = get_data(state);

    if (data.session)
    {
        id = data.session->id();
    }

    lua_pushinteger(state, id);
    return 1;
}

static int lua_get_type_mask(lua_State* state)
{
    const auto& data = get_data(state);

    if (data.buffer)
    {
        uint32_t type = qc_get_type_mask(data.buffer);
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

static int lua_get_operation(lua_State* state)
{
    const auto& data = get_data(state);
    const char* opstring = "";

    if (data.buffer)
    {
        qc_query_op_t op = qc_get_operation(data.buffer);
        opstring = qc_op_to_string(op);
    }

    lua_pushstring(state, opstring);
    return 1;
}

static int lua_get_canonical(lua_State* state)
{
    const auto& data = get_data(state);
    std::string sql;

    if (data.buffer)
    {
        sql = data.buffer->get_sql();
        maxsimd::Markers markers;
        maxsimd::get_canonical(&sql, &markers);
    }

    lua_pushstring(state, sql.c_str());
    return 1;
}

static int lua_get_db(lua_State* state)
{
    const auto& data = get_data(state);
    std::string db;

    if (data.session)
    {
        db = data.session->client_connection()->current_db();
    }

    lua_pushstring(state, db.c_str());
    return 1;
}

static int lua_get_user(lua_State* state)
{
    const auto& data = get_data(state);
    std::string user;

    if (data.session)
    {
        user = data.session->user();
    }

    lua_pushstring(state, user.c_str());
    return 1;
}

static int lua_get_host(lua_State* state)
{
    const auto& data = get_data(state);
    std::string remote;

    if (data.session)
    {
        remote = data.session->client_remote();
    }

    lua_pushstring(state, remote.c_str());
    return 1;
}
}

// static
std::unique_ptr<LuaContext> LuaContext::create(const std::string& script)
{
    std::unique_ptr<LuaContext> rval;

    if (auto state = luaL_newstate())
    {
        luaL_openlibs(state);

        if (luaL_dofile(state, script.c_str()))
        {
            MXS_ERROR("Failed to load script at '%s':%s.", script.c_str(), lua_tostring(state, -1));
            lua_close(state);
        }
        else
        {
            rval.reset(new LuaContext(state));
        }
    }
    else
    {
        MXS_ERROR("Unable to initialize new Lua state.");
    }

    return rval;
}

LuaContext::LuaContext(lua_State* state)
    : m_state(state)
{
    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_session_id, 1);
    lua_setglobal(m_state, "mxs_get_session_id");

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_type_mask, 1);
    lua_setglobal(m_state, "mxs_get_type_mask");

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_operation, 1);
    lua_setglobal(m_state, "mxs_get_operation");

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_canonical, 1);
    lua_setglobal(m_state, "mxs_get_canonical");

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_db, 1);
    lua_setglobal(m_state, "mxs_get_db");

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_user, 1);
    lua_setglobal(m_state, "mxs_get_user");

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_host, 1);
    lua_setglobal(m_state, "mxs_get_host");
}

LuaContext::~LuaContext()
{
    lua_close(m_state);
}

void LuaContext::create_instance()
{
    lua_getglobal(m_state, "createInstance");

    if (lua_pcall(m_state, 0, 0, 0))
    {
        MXS_WARNING("Failed to get global variable 'createInstance':  %s. The createInstance "
                    "entry point will not be called.",
                    lua_tostring(m_state, -1));
        lua_pop(m_state, -1);   // Pop the error off the stack
    }
}

void LuaContext::new_session(MXS_SESSION* session)
{
    Scope scope(this, {session, nullptr});

    /** Call the newSession entry point */
    lua_getglobal(m_state, "newSession");
    lua_pushstring(m_state, session->user().c_str());
    lua_pushstring(m_state, session->client_remote().c_str());

    if (lua_pcall(m_state, 2, 0, 0))
    {
        MXS_WARNING("Failed to get global variable 'newSession': '%s'. The newSession entry "
                    "point will not be called.", lua_tostring(m_state, -1));
        lua_pop(m_state, -1);   // Pop the error off the stack
    }
}

bool LuaContext::route_query(MXS_SESSION* session, GWBUF** buffer)
{
    Scope scope(this, {session, *buffer});
    bool route = true;
    const auto& sql = m_data.buffer->get_sql();

    if (!sql.empty())
    {
        lua_getglobal(m_state, "routeQuery");

        lua_pushstring(m_state, sql.c_str());

        if (lua_pcall(m_state, 1, 1, 0))
        {
            MXS_ERROR("Call to 'routeQuery' failed: '%s'.", lua_tostring(m_state, -1));
            lua_pop(m_state, -1);
        }
        else if (lua_gettop(m_state))
        {
            if (lua_isstring(m_state, -1))
            {
                gwbuf_free(*buffer);
                *buffer = modutil_create_query(lua_tostring(m_state, -1));
            }
            else if (lua_isboolean(m_state, -1))
            {
                route = lua_toboolean(m_state, -1);
            }
        }
    }

    return route;
}

void LuaContext::client_reply(MXS_SESSION* session, const char* target)
{
    Scope scope(this, {session, nullptr});

    lua_getglobal(m_state, "clientReply");

    lua_pushstring(m_state, target);

    if (lua_pcall(m_state, 1, 0, 0))
    {
        MXS_ERROR("Call to 'clientReply' failed: '%s'.", lua_tostring(m_state, -1));
        lua_pop(m_state, -1);
    }
}

void LuaContext::close_session(MXS_SESSION* session)
{
    Scope scope(this, {session, nullptr});

    lua_getglobal(m_state, "closeSession");
    if (lua_pcall(m_state, 0, 0, 0))
    {
        MXS_WARNING("Failed to get global variable 'closeSession': '%s'. The closeSession entry point "
                    "will not be called.",
                    lua_tostring(m_state, -1));
        lua_pop(m_state, -1);
    }
}

std::string LuaContext::diagnostics()
{
    std::string rval;
    lua_getglobal(m_state, "diagnostic");

    if (lua_pcall(m_state, 0, 1, 0) == 0)
    {
        lua_gettop(m_state);

        if (lua_isstring(m_state, -1))
        {
            rval = lua_tostring(m_state, -1);
        }
    }
    else
    {
        lua_pop(m_state, -1);
    }

    return rval;
}
