/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "luacontext.hh"

#include <maxsimd/canonical.hh>
#include <maxbase/alloc.hh>
#include <maxscale/parser.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

namespace
{

//
// Helpers for dealing with the Lua C API
//

const LuaData& get_data(lua_State* state)
{
    return *static_cast<LuaData*>(lua_touserdata(state, lua_upvalueindex(1)));
}

void push_arg(lua_State* state, std::string_view str)
{
    lua_pushlstring(state, str.data(), str.size());
}

void push_arg(lua_State* state, int64_t i)
{
    lua_pushinteger(state, i);
}

template<class ... Args>
bool call_function(lua_State* state, const char* name, int nret, Args ... args)
{
    bool ok = true;
    lua_getglobal(state, name);
    int type = lua_type(state, -1);

    if (type != LUA_TFUNCTION)
    {
        ok = false;
        MXB_WARNING("The '%s' global is not a function but a %s", name, lua_typename(state, type));
        lua_pop(state, -1);     // Pop the value off the stack
    }
    else
    {
        (push_arg(state, std::forward<Args>(args)), ...);
        constexpr int nargs = sizeof...(Args);

        if (lua_pcall(state, nargs, nret, 0))
        {
            MXB_WARNING("The call to '%s' failed: %s", name, lua_tostring(state, -1));
            lua_pop(state, -1);     // Pop the error off the stack
            ok = false;
        }
    }

    return ok;
}

//
// Functions that are exposed to the Lua environment
//

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
        uint32_t type = data.session->client_connection()->parser()->get_type_mask(*data.buffer);
        std::string mask = mxs::Parser::type_mask_to_string(type);
        lua_pushstring(state, mask.c_str());
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
        mxs::sql::OpCode op = data.session->client_connection()->parser()->get_operation(*data.buffer);
        opstring = mxs::sql::to_string(op);
    }

    lua_pushstring(state, opstring);
    return 1;
}

static int lua_get_canonical(lua_State* state)
{
    const auto& data = get_data(state);
    std::string sql;

    if (data.session && data.buffer)
    {
        sql = std::string(data.session->client_connection()->parser()->get_sql(*data.buffer));
        maxsimd::get_canonical(&sql);
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
            MXB_ERROR("Failed to load script at '%s':%s.", script.c_str(), lua_tostring(state, -1));
            lua_close(state);
        }
        else
        {
            rval.reset(new LuaContext(state));
        }
    }
    else
    {
        MXB_ERROR("Unable to initialize new Lua state.");
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

void LuaContext::create_instance(const std::string& name)
{
    call_function(m_state, "createInstance", 0, name);
}

void LuaContext::new_session(MXS_SESSION* session)
{
    Scope scope(this, {session, nullptr});
    call_function(m_state, "newSession", 0, session->user(), session->client_remote());
}

bool LuaContext::route_query(MXS_SESSION* session, GWBUF* buffer)
{
    Scope scope(this, {session, buffer});
    bool route = true;
    std::string_view sql = session->protocol()->get_sql(*m_data.buffer);

    if (call_function(m_state, "routeQuery", 1, sql))
    {
        if (lua_gettop(m_state))
        {
            if (lua_isstring(m_state, -1))
            {
                *buffer = mariadb::create_query(lua_tostring(m_state, -1));
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
    call_function(m_state, "clientReply", 0, target);
}

void LuaContext::close_session(MXS_SESSION* session)
{
    Scope scope(this, {session, nullptr});
    call_function(m_state, "closeSession", 0);
}

std::string LuaContext::diagnostics()
{
    std::string rval;

    if (call_function(m_state, "diagnostic", 1))
    {
        lua_gettop(m_state);

        if (lua_isstring(m_state, -1))
        {
            rval = lua_tostring(m_state, -1);
        }
    }

    return rval;
}
