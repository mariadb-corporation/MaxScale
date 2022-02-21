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
static int id_gen(lua_State* state)
{
    static int id_pool = 0;
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
        std::string sql = buf->get_sql();
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
    /** Expose an ID generation function */
    lua_pushcfunction(m_state, id_gen);
    lua_setglobal(m_state, "id_gen");

    /** Expose a part of the query classifier API */
    lua_pushlightuserdata(m_state, &m_query);
    lua_pushcclosure(m_state, lua_qc_get_type_mask, 1);
    lua_setglobal(m_state, "lua_qc_get_type_mask");

    lua_pushlightuserdata(m_state, &m_query);
    lua_pushcclosure(m_state, lua_qc_get_operation, 1);
    lua_setglobal(m_state, "lua_qc_get_operation");

    lua_pushlightuserdata(m_state, &m_query);
    lua_pushcclosure(m_state, lua_get_canonical, 1);
    lua_setglobal(m_state, "lua_get_canonical");
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

bool LuaContext::route_query(GWBUF** buffer)
{
    bool route = true;
    m_query = *buffer;
    const auto& sql = m_query->get_sql();

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

    m_query = nullptr;

    return route;
}

void LuaContext::client_reply()
{
    lua_getglobal(m_state, "clientReply");

    if (lua_pcall(m_state, 0, 0, 0))
    {
        MXS_ERROR("Call to 'clientReply' failed: '%s'.", lua_tostring(m_state, -1));
        lua_pop(m_state, -1);
    }
}

void LuaContext::close_session()
{
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
