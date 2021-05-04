/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "hintrouter"
#include "hintrouter.hh"

#include <limits>
#include <vector>

#include <maxbase/atomic.hh>

namespace
{
namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::ROUTER);

cfg::ParamEnum<HINT_TYPE> s_default_action(
    &s_spec, "default_action", "Default action to take",
    {
        {HINT_ROUTE_TO_MASTER, "master", },
        {HINT_ROUTE_TO_SLAVE, "slave", },
        {HINT_ROUTE_TO_NAMED_SERVER, "named", },
        {HINT_ROUTE_TO_ALL, "all", },
    }, HINT_ROUTE_TO_MASTER);

cfg::ParamString s_default_server(
    &s_spec, "default_server", "Default server to use", "");

cfg::ParamInteger s_max_slaves(
    &s_spec, "max_slaves", "Maximum number of slave servers to use", -1);
}

HintRouter::Config::Config(const char* name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&Config::default_action, &s_default_action);
    add_native(&Config::default_server, &s_default_server);
    add_native(&Config::max_slaves, &s_max_slaves);
}

HintRouter::HintRouter(SERVICE* pService)
    : m_routed_to_master(0)
    , m_routed_to_slave(0)
    , m_routed_to_named(0)
    , m_routed_to_all(0)
    , m_total_slave_conns(0)
    , m_config(pService->name())
{
    HR_ENTRY();
    MXS_NOTICE("Hint router [%s] created.", pService->name());
}

// static
HintRouter* HintRouter::create(SERVICE* pService)
{
    HR_ENTRY();
    return new HintRouter(pService);
}

HintRouterSession* HintRouter::newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
{
    typedef HintRouterSession::BackendArray::size_type array_index;
    HR_ENTRY();
    int64_t max_slaves = m_config.max_slaves < 0 ?
        pSession->service->get_children().size() - 1 :
        m_config.max_slaves;

    HintRouterSession::BackendMap all_backends;
    all_backends.rehash(1 + max_slaves);
    HintRouterSession::BackendArray slave_arr;
    slave_arr.reserve(max_slaves);

    mxs::Endpoint* master_ref = NULL;
    HintRouterSession::BackendArray slave_refs;
    slave_refs.reserve(max_slaves);

    if (master_ref)
    {
        connect_to_backend(pSession, master_ref, &all_backends);
    }

    /* Different sessions may use different slaves if the 'max_session_slaves'-
     * setting is low enough. First, set maximal looping limits noting that the
     * array is treated as a ring. Also, array size may have changed since last
     * time it was formed. */
    if (slave_refs.size())
    {
        array_index size = slave_refs.size();
        array_index begin = m_total_slave_conns % size;
        array_index limit = begin + size;

        int slave_conns = 0;
        array_index current = begin;
        for (;
             (slave_conns < max_slaves) && current != limit;
             current++)
        {
            auto slave_ref = slave_refs.at(current % size);

            if (connect_to_backend(pSession, slave_ref, &all_backends))
            {
                slave_arr.push_back(slave_ref);
                slave_conns++;
            }
        }
        m_total_slave_conns += slave_conns;
    }

    HintRouterSession* rval = NULL;
    if (all_backends.size() != 0)
    {
        rval = new HintRouterSession(pSession, this, all_backends);
    }
    return rval;
}

json_t* HintRouter::diagnostics() const
{
    HR_ENTRY();

    json_t* rval = json_object();

    json_object_set_new(rval, "total_slave_connections", json_integer(m_total_slave_conns));
    json_object_set_new(rval, "route_master", json_integer(m_routed_to_master));
    json_object_set_new(rval, "route_slave", json_integer(m_routed_to_slave));
    json_object_set_new(rval, "route_named_server", json_integer(m_routed_to_named));
    json_object_set_new(rval, "route_all", json_integer(m_routed_to_all));

    return rval;
}

bool HintRouter::connect_to_backend(MXS_SESSION* session,
                                    mxs::Endpoint* sref,
                                    HintRouterSession::BackendMap* all_backends)
{
    bool result = false;

    if (sref->connect())
    {
        HR_DEBUG("Connected.");
        (*all_backends)[sref->target()->name()] = sref;
        result = true;
    }
    else
    {
        HR_DEBUG("Connection failed.");
    }

    return result;
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE module =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::BETA,
        MXS_ROUTER_VERSION,
        "A hint router",
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RESULTSET_OUTPUT,
        &mxs::RouterApi<HintRouter>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {MXS_END_MODULE_PARAMS}
        },
        &s_spec
    };
    return &module;
}
