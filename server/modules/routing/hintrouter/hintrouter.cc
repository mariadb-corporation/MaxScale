/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
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

static const MXS_ENUM_VALUE default_action_values[] =
{
    {"master", HINT_ROUTE_TO_MASTER      },
    {"slave",  HINT_ROUTE_TO_SLAVE       },
    {"named",  HINT_ROUTE_TO_NAMED_SERVER},
    {"all",    HINT_ROUTE_TO_ALL         },
    {NULL}      /* Last must be NULL */
};
static const char DEFAULT_ACTION[] = "default_action";
static const char DEFAULT_SERVER[] = "default_server";
static const char MAX_SLAVES[] = "max_slaves";

HintRouter::HintRouter(SERVICE* pService, HINT_TYPE default_action, string& default_server, int max_slaves)
    : m_routed_to_master(0)
    , m_routed_to_slave(0)
    , m_routed_to_named(0)
    , m_routed_to_all(0)
    , m_default_action(default_action)
    , m_default_server(default_server)
    , m_max_slaves(max_slaves)
    , m_total_slave_conns(0)
{
    HR_ENTRY();
    if (m_max_slaves < 0)
    {
        // set a reasonable default value
        m_max_slaves = pService->get_children().size() - 1;
    }
    MXS_NOTICE("Hint router [%s] created.", pService->name());
}

// static
HintRouter* HintRouter::create(SERVICE* pService, mxs::ConfigParameters* params)
{
    HR_ENTRY();

    HINT_TYPE default_action = (HINT_TYPE)params->get_enum(DEFAULT_ACTION, default_action_values);
    string default_server = params->get_string(DEFAULT_SERVER);
    int max_slaves = params->get_integer(MAX_SLAVES);
    return new HintRouter(pService, default_action, default_server, max_slaves);
}

HintRouterSession* HintRouter::newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
{
    typedef HintRouterSession::BackendArray::size_type array_index;
    HR_ENTRY();
    HintRouterSession::BackendMap all_backends;
    all_backends.rehash(1 + m_max_slaves);
    HintRouterSession::BackendArray slave_arr;
    slave_arr.reserve(m_max_slaves);

    mxs::Endpoint* master_ref = NULL;
    HintRouterSession::BackendArray slave_refs;
    slave_refs.reserve(m_max_slaves);

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
             (slave_conns < m_max_slaves) && current != limit;
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
    json_t* arr = json_array();

    for (int i = 0; default_action_values[i].name; i++)
    {
        if (default_action_values[i].enum_value == (uint64_t)m_default_action)
        {
            json_array_append_new(arr, json_string(default_action_values[i].name));
        }
    }

    json_object_set_new(rval, "default_action", arr);
    json_object_set_new(rval, "default_server", json_string(m_default_server.c_str()));
    json_object_set_new(rval, "max_slave_connections", json_integer(m_max_slaves));
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
            {
                DEFAULT_ACTION,
                MXS_MODULE_PARAM_ENUM,
                default_action_values[0].name,
                MXS_MODULE_OPT_NONE,
                default_action_values
            },
            {DEFAULT_SERVER,                              MXS_MODULE_PARAM_SERVER,""  },
            {MAX_SLAVES,                                  MXS_MODULE_PARAM_INT,  "-1"},
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &module;
}
