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

#include <string.h>
#include <dlfcn.h>
#include <mutex>
#include <maxbase/alloc.h>
#include <maxscale/config_common.hh>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/session.hh>

#include "luacontext.hh"

namespace
{

namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamPath s_global_script(
    &s_spec, "global_script", "Path to global level Lua script",
    cfg::ParamPath::R, "");

cfg::ParamPath s_session_script(
    &s_spec, "session_script", "Path to session level Lua script",
    cfg::ParamPath::R, "");
}

class LuaFilterSession;
class LuaFilter;

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

    mxs::FilterSession* newSession(MXS_SESSION* session, SERVICE* service) override;

    json_t*  diagnostics() const override;
    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    void new_session(MXS_SESSION* session);
    bool route_query(MXS_SESSION* session, GWBUF** buffer);
    void client_reply(MXS_SESSION* session, const char* target);
    void close_session(MXS_SESSION* session);

    bool post_configure();

    mutable std::mutex m_lock;

private:

    LuaFilter(const char* name)
        : m_config(this, name)
    {
    }

    std::unique_ptr<LuaContext> m_context;
    Config                      m_config;
};

/**
 * The session structure for Lua filter.
 */
class LuaFilterSession : public maxscale::FilterSession
{
public:
    LuaFilterSession(MXS_SESSION* session, SERVICE* service,
                     LuaFilter* filter, std::unique_ptr<LuaContext> context);
    ~LuaFilterSession();

    bool routeQuery(GWBUF* queue) override;
    bool clientReply(GWBUF* queue, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    LuaFilter*                  m_filter;
    std::unique_ptr<LuaContext> m_context;
};

LuaFilterSession::LuaFilterSession(MXS_SESSION* session, SERVICE* service,
                                   LuaFilter* filter, std::unique_ptr<LuaContext> context)
    : FilterSession(session, service)
    , m_filter(filter)
    , m_context(std::move(context))
{
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
        if (auto context = LuaContext::create(m_config.global_script))
        {
            m_context = std::move(context);
            m_context->create_instance(m_config.name());
        }
        else
        {
            error = true;
        }
    }

    return !error;
}

uint64_t LuaFilter::getCapabilities() const
{
    return RCAP_TYPE_STMT_INPUT;
}

mxs::FilterSession* LuaFilter::newSession(MXS_SESSION* session, SERVICE* service)
{
    bool ok = true;
    std::unique_ptr<LuaContext> context;

    if (!m_config.session_script.empty())
    {
        if ((context = LuaContext::create(m_config.session_script)))
        {
            context->new_session(session);
        }
        else
        {
            ok = false;
        }
    }

    LuaFilterSession* rval = nullptr;

    if (ok)
    {
        rval = new LuaFilterSession(session, service, this, std::move(context));

        if (m_context)
        {
            m_context->new_session(session);
        }
    }

    return rval;
}

LuaFilterSession::~LuaFilterSession()
{
    if (m_context)
    {
        m_context->close_session(m_pSession);
    }

    m_filter->close_session(m_pSession);
}

bool LuaFilterSession::clientReply(GWBUF* queue, const maxscale::ReplyRoute& down, const mxs::Reply& reply)
{
    const char* target = down.empty() ? "" : down.front()->target()->name();

    if (m_context)
    {
        m_context->client_reply(m_pSession, target);
    }

    m_filter->client_reply(m_pSession, target);

    return FilterSession::clientReply(queue, down, reply);
}

bool LuaFilterSession::routeQuery(GWBUF* queue)
{
    bool route = true;

    if (m_context)
    {
        route = m_context->route_query(m_pSession, &queue);
    }

    if (route)
    {
        m_filter->route_query(m_pSession, &queue);
    }

    bool ok = true;

    if (route)
    {
        ok = FilterSession::routeQuery(queue);
    }
    else
    {
        gwbuf_free(queue);
        GWBUF* err = modutil_create_mysql_err_msg(1, 0, 1045, "28000", "Access denied.");
        FilterSession::set_response(err);
    }

    return ok;
}

json_t* LuaFilter::diagnostics() const
{
    json_t* rval = json_object();

    if (m_context)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto str = m_context->diagnostics();

        if (!str.empty())
        {
            json_object_set_new(rval, "script_output", json_string(str.c_str()));
        }
    }

    return rval;
}

void LuaFilter::new_session(MXS_SESSION* session)
{
    std::lock_guard guard(m_lock);

    if (m_context)
    {
        m_context->new_session(session);
    }
}

bool LuaFilter::route_query(MXS_SESSION* session, GWBUF** buffer)
{
    bool rval = true;
    std::lock_guard guard(m_lock);

    if (m_context)
    {
        rval = m_context->route_query(session, buffer);
    }

    return rval;
}

void LuaFilter::client_reply(MXS_SESSION* session, const char* target)
{
    std::lock_guard guard(m_lock);

    if (m_context)
    {
        m_context->client_reply(session, target);
    }
}

void LuaFilter::close_session(MXS_SESSION* session)
{
    std::lock_guard guard(m_lock);

    if (m_context)
    {
        m_context->close_session(session);
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
        &s_spec
    };

    // Some luarocks libraries (e.g. lpeg) do not dynamically link to the lua libraries and expect the symbols
    // to be globally available. The correct way to solve this would be to rebuild the luarocks library and
    // link it against the lua libraries but this isn't a realistic option. Luckily, lua uses dlopen to load
    // modules and we can inject the symbols from it by loading the liblua.so library with dlopen using
    // RTLD_GLOBAL. This globally exposes the symbols from it to all subsequent dynamically loaded libraries
    // which pretty much amounts to the same as recompiling the libraries and linking them against liblua.so.
    if (void* handle = dlopen("liblua.so", RTLD_NOW | RTLD_GLOBAL))
    {
        dlclose(handle);
    }
    else
    {
        MXS_WARNING("Failed to load the core Lua library: %s. Some external Lua libraries might not work "
                    "as a result of this. The core Lua library can be manually loaded by using "
                    "LD_PRELOAD and pointing it at the correct 'liblua.so' library.", dlerror());
    }

    return &info;
}
}
