/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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

const char* CN_CREATE_INSTANCE = "createInstance";
const char* CN_NEW_SESSON = "newSession";
const char* CN_ROUTE_QUERY = "routeQuery";
const char* CN_CLIENT_REPLY = "clientReply";
const char* CN_CLOSE_SESSION = "closeSession";
const char* CN_DIAGNOSTIC = "diagnostic";

//
// Helpers for dealing with the Lua C API
//

inline void check_precondition(lua_State* state, bool value)
{
    if (!value)
    {
        lua_pushstring(state, "Function called outside of the correct callback function");
        lua_error(state);   // lua_error does a longjump and doesn't return
    }
}

const LuaData& get_data(lua_State* state)
{
    LuaData* data = static_cast<LuaData*>(lua_touserdata(state, lua_upvalueindex(1)));
    check_precondition(state, data);
    return *data;
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
bool call_pushed_function(lua_State* state, const char* name, int nret, Args ... args)
{
    bool ok = true;
    mxb_assert(lua_type(state, -1) == LUA_TFUNCTION);

    (push_arg(state, std::forward<Args>(args)), ...);
    constexpr int nargs = sizeof...(Args);

    if (lua_pcall(state, nargs, nret, 0))
    {
        MXB_WARNING("The call to '%s' failed: %s", name, lua_tostring(state, -1));
        lua_pop(state, -1);     // Pop the error off the stack
        ok = false;
    }

    return ok;
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
        ok = call_pushed_function(state, name, nret, std::forward<Args>(args)...);
    }

    return ok;
}

template<class ... Args>
bool call_function(lua_State* state, int ref, const char* name, int nret, Args ... args)
{
    if (ref == LUA_REFNIL)
    {
        return false;
    }

    lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
    mxb_assert(lua_type(state, -1) == LUA_TFUNCTION);
    return call_pushed_function(state, name, nret, std::forward<Args>(args)...);
}

int get_function_ref(lua_State* state, const char* name)
{
    int rv = LUA_REFNIL;
    lua_getglobal(state, name);
    int type = lua_type(state, -1);

    if (type != LUA_TFUNCTION)
    {
        MXB_WARNING("The '%s' global is not a function but a %s", name, lua_typename(state, type));
        lua_pop(state, -1);     // Pop the value off the stack
    }
    else
    {
        rv = luaL_ref(state, LUA_REGISTRYINDEX);
    }

    return rv;
}

//
// Functions that are exposed to the Lua environment
//

static int lua_get_session_id(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session);

    lua_pushinteger(state, data.session->id());
    return 1;
}

static int lua_get_type_mask(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session && data.buffer);

    uint32_t type = data.session->client_connection()->parser()->get_type_mask(*data.buffer);
    std::string mask = mxs::Parser::type_mask_to_string(type);
    lua_pushlstring(state, mask.data(), mask.size());
    return 1;
}

static int lua_get_operation(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session && data.buffer);

    mxs::sql::OpCode op = data.session->client_connection()->parser()->get_operation(*data.buffer);
    lua_pushstring(state, mxs::sql::to_string(op));
    return 1;
}

static int lua_get_canonical(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session && data.buffer);

    auto sql = data.session->client_connection()->parser()->get_canonical(*data.buffer);
    lua_pushlstring(state, sql.data(), sql.size());


    return 1;
}

static int lua_get_db(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session);

    std::string db = data.session->client_connection()->current_db();
    lua_pushlstring(state, db.data(), db.size());

    return 1;
}

static int lua_get_user(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session);

    const auto& user = data.session->user();
    lua_pushlstring(state, user.data(), user.size());

    return 1;
}

static int lua_get_host(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session);

    const auto& remote = data.session->client_remote();
    lua_pushlstring(state, remote.data(), remote.size());

    return 1;
}

static int lua_get_sql(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.session && data.buffer);

    auto sql = data.session->client_connection()->parser()->get_sql(*data.buffer);
    lua_pushlstring(state, sql.data(), sql.size());

    return 1;
}

static int lua_get_replier(lua_State* state)
{
    const auto& data = get_data(state);
    check_precondition(state, data.target);

    lua_pushstring(state, data.target);
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

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_sql, 1);
    lua_setglobal(m_state, "mxs_get_sql");

    lua_pushlightuserdata(m_state, &m_data);
    lua_pushcclosure(m_state, lua_get_replier, 1);
    lua_setglobal(m_state, "mxs_get_replier");

    m_client_reply = get_function_ref(m_state, "clientReply");
    m_route_query = get_function_ref(m_state, "routeQuery");
}

LuaContext::~LuaContext()
{
    for (int ref : {m_client_reply, m_route_query})
    {
        if (ref != LUA_REFNIL)
        {
            luaL_unref(m_state, LUA_REGISTRYINDEX, ref);
        }
    }

    lua_close(m_state);
}

void LuaContext::create_instance(const std::string& name)
{
    m_data = LuaData{nullptr, nullptr, nullptr};
    call_function(m_state, CN_CREATE_INSTANCE, 0, name);
}

void LuaContext::new_session(MXS_SESSION* session)
{
    m_data = LuaData{session, nullptr, nullptr};
    call_function(m_state, CN_NEW_SESSON, 0, session->user(), session->client_remote());
}

bool LuaContext::route_query(MXS_SESSION* session, GWBUF* buffer)
{
    m_data = LuaData{session, buffer, nullptr};
    bool route = true;

    if (call_function(m_state, m_route_query, CN_ROUTE_QUERY, 1))
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
    m_data = LuaData{session, nullptr, target};
    call_function(m_state, m_client_reply, CN_CLIENT_REPLY, 0);
}

void LuaContext::close_session(MXS_SESSION* session)
{
    m_data = LuaData{session, nullptr, nullptr};
    call_function(m_state, CN_CLOSE_SESSION, 0);
}

std::string LuaContext::diagnostics()
{
    std::string rval;
    m_data = LuaData{nullptr, nullptr, nullptr};

    if (call_function(m_state, CN_DIAGNOSTIC, 1))
    {
        lua_gettop(m_state);

        if (lua_isstring(m_state, -1))
        {
            rval = lua_tostring(m_state, -1);
        }
    }

    return rval;
}
