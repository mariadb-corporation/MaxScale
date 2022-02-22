/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#pragma once

#include <string>
#include <memory>

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
};

class LuaContext
{
public:
    static std::unique_ptr<LuaContext> create(const std::string& script);

    ~LuaContext();

    // API methods
    void        create_instance(const std::string& name);
    void        new_session(MXS_SESSION* session);
    bool        route_query(MXS_SESSION* session, GWBUF** buffer);
    void        client_reply(MXS_SESSION* session, const char* target);
    void        close_session(MXS_SESSION* session);
    std::string diagnostics();

private:
    LuaContext(lua_State* state);

    // Helper class for making sure the data is reset to a known good state after each function call
    struct Scope
    {
        Scope(LuaContext* ctx, LuaData new_data)
            : m_ctx(ctx)
            , m_data(std::exchange(ctx->m_data, new_data))
        {
        }

        ~Scope()
        {
            m_ctx->m_data = m_data;
        }

        LuaContext* m_ctx;
        LuaData     m_data;
    };

    LuaData    m_data;
    lua_State* m_state {nullptr};
};
