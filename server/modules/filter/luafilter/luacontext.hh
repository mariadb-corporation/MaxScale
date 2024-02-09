/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>
#include <memory>
#include <utility>

#include <maxscale/session.hh>
#include <maxscale/buffer.hh>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

struct LuaData
{
    MXS_SESSION* session = nullptr;
    GWBUF*       buffer = nullptr;
    const char*  target = nullptr;
};

class LuaContext
{
public:
    static std::unique_ptr<LuaContext> create(const std::string& script);

    ~LuaContext();

    // API methods
    void        create_instance(const std::string& name);
    void        new_session(MXS_SESSION* session);
    bool        route_query(MXS_SESSION* session, GWBUF* buffer);
    void        client_reply(MXS_SESSION* session, const char* target);
    void        close_session(MXS_SESSION* session);
    std::string diagnostics();

private:
    LuaContext(lua_State* state);

    LuaData    m_data;
    lua_State* m_state {nullptr};

    // References to the commonly used global functions
    int m_route_query {LUA_REFNIL};
    int m_client_reply {LUA_REFNIL};
};
