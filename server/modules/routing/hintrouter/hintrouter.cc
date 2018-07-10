/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "hintrouter"
#include "hintrouter.hh"

#include <limits>
#include <vector>

#include <maxscale/log_manager.h>
#include "dcb.hh"

static const MXS_ENUM_VALUE default_action_values[] =
{
    {"master", HINT_ROUTE_TO_MASTER},
    {"slave", HINT_ROUTE_TO_SLAVE},
    {"named", HINT_ROUTE_TO_NAMED_SERVER},
    {"all", HINT_ROUTE_TO_ALL},
    {NULL} /* Last must be NULL */
};
static const char DEFAULT_ACTION[] = "default_action";
static const char DEFAULT_SERVER[] = "default_server";
static const char MAX_SLAVES[] = "max_slaves";

HintRouter::HintRouter(SERVICE* pService, HINT_TYPE default_action, string& default_server,
                       int max_slaves)
    : maxscale::Router<HintRouter, HintRouterSession>(pService),
      m_routed_to_master(0),
      m_routed_to_slave(0),
      m_routed_to_named(0),
      m_routed_to_all(0),
      m_default_action(default_action),
      m_default_server(default_server),
      m_max_slaves(max_slaves),
      m_total_slave_conns(0)
{
    HR_ENTRY();
    if (m_max_slaves < 0)
    {
        // set a reasonable default value
        m_max_slaves = pService->n_dbref - 1;
    }
    MXS_NOTICE("Hint router [%s] created.", pService->name);
}

//static
HintRouter* HintRouter::create(SERVICE* pService, MXS_CONFIG_PARAMETER* params)
{
    HR_ENTRY();

    HINT_TYPE default_action = (HINT_TYPE)config_get_enum(params, DEFAULT_ACTION,
                                                          default_action_values);
    string default_server(config_get_string(params, DEFAULT_SERVER));
    int max_slaves = config_get_integer(params, MAX_SLAVES);
    return new HintRouter(pService, default_action, default_server, max_slaves);
}

HintRouterSession* HintRouter::newSession(MXS_SESSION *pSession)
{
    typedef HintRouterSession::RefArray::size_type array_index;
    HR_ENTRY();
    Dcb master_Dcb(NULL);
    HintRouterSession::BackendMap all_backends;
    all_backends.rehash(1 + m_max_slaves);
    HintRouterSession::BackendArray slave_arr;
    slave_arr.reserve(m_max_slaves);

    SERVER_REF* master_ref = NULL;
    HintRouterSession::RefArray slave_refs;
    slave_refs.reserve(m_max_slaves);

    /* Go through the server references, find master and slaves */
    for (SERVER_REF* pSref = pSession->service->dbref; pSref; pSref = pSref->next)
    {
        if (SERVER_REF_IS_ACTIVE(pSref))
        {
            if (SERVER_IS_MASTER(pSref->server))
            {
                if (!master_ref)
                {
                    master_ref = pSref;
                }
                else
                {
                    MXS_WARNING("Found multiple master servers when creating session.\n");
                }
            }
            else if (SERVER_IS_SLAVE(pSref->server))
            {
                slave_refs.push_back(pSref);
            }
        }
    }

    if (master_ref)
    {
        // Connect to master
        master_Dcb = connect_to_backend(pSession, master_ref, &all_backends);
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
            SERVER_REF* slave_ref = slave_refs.at(current % size);
            Dcb slave_conn = connect_to_backend(pSession, slave_ref, &all_backends);
            if (slave_conn.get())
            {
                slave_arr.push_back(slave_conn);
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

void HintRouter::diagnostics(DCB* pOut)
{
    HR_ENTRY();
    for (int i = 0; default_action_values[i].name; i++)
    {
        if (default_action_values[i].enum_value == (uint64_t)m_default_action)
        {
            dcb_printf(pOut, "\tDefault action: route to %s\n", default_action_values[i].name);
        }
    }
    dcb_printf(pOut, "\tDefault server: %s\n", m_default_server.c_str());
    dcb_printf(pOut, "\tMaximum slave connections/session: %d\n", m_max_slaves);
    dcb_printf(pOut, "\tTotal cumulative slave connections: %d\n", m_total_slave_conns);
    dcb_printf(pOut, "\tQueries routed to master: %d\n", m_routed_to_master);
    dcb_printf(pOut, "\tQueries routed to single slave: %d\n", m_routed_to_slave);
    dcb_printf(pOut, "\tQueries routed to named server: %d\n", m_routed_to_named);
    dcb_printf(pOut, "\tQueries routed to all servers: %d\n", m_routed_to_all);
}

json_t* HintRouter::diagnostics_json() const
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

Dcb HintRouter::connect_to_backend(MXS_SESSION* session, SERVER_REF* sref,
                                   HintRouterSession::BackendMap* all_backends)
{
    Dcb result(NULL);
    HR_DEBUG("Connecting to %s.", sref->server->name);
    DCB* new_connection = dcb_connect(sref->server, session, sref->server->protocol);

    if (new_connection)
    {
        HR_DEBUG("Connected.");
        atomic_add(&sref->connections, 1);
        new_connection->service = session->service;

        result = Dcb(new_connection);
        string name(new_connection->server->name);
        all_backends->insert(HintRouterSession::MapElement(name, result));
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
        MXS_MODULE_API_ROUTER,   /* Module type */
        MXS_MODULE_BETA_RELEASE, /* Release status */
        MXS_ROUTER_VERSION,      /* Implemented module API version */
        "A hint router", /* Description */
        "V1.0.0", /* Module version */
        RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RESULTSET_OUTPUT,
        &HintRouter::s_object,
        NULL, /* Process init, can be null */
        NULL, /* Process finish, can be null */
        NULL, /* Thread init */
        NULL, /* Thread finish */
        {
            {
                DEFAULT_ACTION,
                MXS_MODULE_PARAM_ENUM,
                default_action_values[0].name,
                MXS_MODULE_OPT_NONE,
                default_action_values
            },
            {DEFAULT_SERVER, MXS_MODULE_PARAM_SERVER, ""},
            {MAX_SLAVES, MXS_MODULE_PARAM_INT, "-1"},
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &module;
}
