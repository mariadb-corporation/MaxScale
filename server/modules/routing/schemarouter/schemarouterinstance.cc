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

#include "schemarouter.hh"
#include "schemarouterinstance.hh"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <maxscale/buffer.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/router.hh>
#include <maxscale/secrets.hh>

using std::string;

namespace
{
namespace cfg = mxs::config;

cfg::Specification s_spec(MXB_MODULE_NAME, cfg::Specification::ROUTER);

cfg::ParamStringList s_ignore_tables(
    &s_spec, "ignore_tables", "List of tables to ignore when checking for duplicates",
    ",", cfg::ParamStringList::value_type{}, cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_ignore_tables_regex(
    &s_spec, "ignore_tables_regex", "Regex of tables to ignore when checking for duplicates",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamBool s_refresh_databases(
    &s_spec, "refresh_databases", "Refresh database mapping information",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_refresh_interval(
    &s_spec, "refresh_interval", "How often to refresh the database mapping information",
    std::chrono::seconds(300), cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_max_staleness(
    &s_spec, "max_staleness",
    "Maximum allowed staleness of a database map entry before clients block and wait for an update",
    std::chrono::seconds(150), cfg::Param::AT_RUNTIME);

cfg::ParamBool s_debug(
    &s_spec, "debug", "Enable debug mode",
    false, cfg::Param::AT_RUNTIME);
}

namespace schemarouter
{

Config::Config(const char* name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&Config::m_v, &Values::ignore_tables, &s_ignore_tables);
    add_native(&Config::m_v, &Values::ignore_tables_regex, &s_ignore_tables_regex);
    add_native(&Config::m_v, &Values::refresh_databases, &s_refresh_databases);
    add_native(&Config::m_v, &Values::refresh_interval, &s_refresh_interval);
    add_native(&Config::m_v, &Values::max_staleness, &s_max_staleness);
    add_native(&Config::m_v, &Values::debug, &s_debug);
}

/**
 * @file schemarouter.c The entry points for the simple sharding router module.
 */

SchemaRouter::SchemaRouter(SERVICE* service)
    : m_config(service->name())
    , m_service(service)
{
}

SchemaRouter* SchemaRouter::create(SERVICE* pService)
{
    return new SchemaRouter(pService);
}

/**
 * @node Search all RUNNING backend servers and connect
 *
 * Parameters:
 * @param backend_ref - in, use, out
 *      Pointer to backend server reference object array.
 *      NULL is not allowed.
 *
 * @param router_nservers - in, use
 *      Number of backend server pointers pointed to by b.
 *
 * @param session - in, use
 *      MaxScale session pointer used when connection to backend is established.
 *
 * @param  router - in, use
 *      Pointer to router instance. Used when server states are qualified.
 *
 * @return true, if at least one master and one slave was found.
 *
 *
 * @details It is assumed that there is only one available server.
 *      There will be exactly as many backend references than there are
 *      connections because all servers are supposed to be operational. It is,
 *      however, possible that there are less available servers than expected.
 */
bool connect_backend_servers(SRBackendList& backends, MXS_SESSION* session)
{
    bool succp = false;
    int servers_connected = 0;

    /**
     * Scan server list and connect each of them. None should fail or session
     * can't be established.
     */
    for (const auto& b : backends)
    {
        if (b->target()->is_connectable())
        {
            /** New server connection */
            if (!b->in_use())
            {
                if (b->connect())
                {
                    servers_connected += 1;
                }
                else
                {
                    succp = false;
                    MXB_ERROR("Unable to establish "
                              "connection with slave '%s'",
                              b->name());
                    /* handle connect error */
                    break;
                }
            }
        }
    }

    if (servers_connected > 0)
    {
        succp = true;

        if (mxb_log_should_log(LOG_INFO))
        {
            for (const auto& b : backends)
            {
                if (b->in_use())
                {
                    MXB_INFO("Connected %s in \t'%s'",
                             b->target()->status_string().c_str(),
                             b->name());
                }
            }
        }
    }

    return succp;
}

mxs::RouterSession* SchemaRouter::newSession(MXS_SESSION* pSession, const Endpoints& endpoints)
{
    SRBackendList backends;

    for (auto e : endpoints)
    {
        backends.emplace_back(new SRBackend(e));
    }

    SchemaRouterSession* rval = NULL;

    if (connect_backend_servers(backends, pSession))
    {
        rval = new SchemaRouterSession(pSession, this, std::move(backends));
    }
    else
    {
        MXB_ERROR("Failed to connect to any of the backend servers");
    }


    return rval;
}

json_t* SchemaRouter::diagnostics() const
{
    double sescmd_pct = m_stats.n_sescmd != 0 ?
        100.0 * ((double)m_stats.n_sescmd / (double)m_stats.n_queries) :
        0.0;

    json_t* rval = json_object();
    json_object_set_new(rval, "queries", json_integer(m_stats.n_queries));
    json_object_set_new(rval, "sescmd_percentage", json_real(sescmd_pct));
    json_object_set_new(rval, "longest_sescmd_chain", json_integer(m_stats.longest_sescmd));
    json_object_set_new(rval, "times_sescmd_limit_exceeded", json_integer(m_stats.n_hist_exceeded));

    /** Session time statistics */
    if (m_stats.sessions > 0)
    {
        json_object_set_new(rval, "longest_session", json_real(m_stats.ses_longest));
        json_object_set_new(rval, "shortest_session", json_real(m_stats.ses_shortest));
        json_object_set_new(rval, "average_session", json_real(m_stats.ses_average));
    }

    auto stats = m_shard_manager.stats();
    json_object_set_new(rval, "shard_map_hits", json_integer(stats.hits));
    json_object_set_new(rval, "shard_map_misses", json_integer(stats.misses));
    json_object_set_new(rval, "shard_map_updates", json_integer(stats.updates));
    json_object_set_new(rval, "shard_map_stale", json_integer(stats.stale));

    return rval;
}

static const uint64_t CAPABILITIES = RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RUNTIME_CONFIG
    | RCAP_TYPE_REQUEST_TRACKING | RCAP_TYPE_QUERY_CLASSIFICATION | RCAP_TYPE_SESCMD_HISTORY
    | RCAP_TYPE_OLD_PROTOCOL;

uint64_t SchemaRouter::getCapabilities() const
{
    return schemarouter::CAPABILITIES;
}

// static
bool SchemaRouter::clear_shards(const MODULECMD_ARG* argv, json_t** output)
{
    SchemaRouter* router = static_cast<SchemaRouter*>(argv->argv[0].value.service->router());
    router->m_shard_manager.clear();
    return true;
}

// static
bool SchemaRouter::invalidate_shards(const MODULECMD_ARG* argv, json_t** output)
{
    SchemaRouter* router = static_cast<SchemaRouter*>(argv->argv[0].value.service->router());
    router->m_shard_manager.invalidate();
    return true;
}
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static auto desc = "A database sharding router for simple sharding";
    auto* api_ptr = &mxs::RouterApi<schemarouter::SchemaRouter>::s_api;

    static modulecmd_arg_type_t cmd_args[] =
    {
        {MODULECMD_ARG_SERVICE | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "The schemarouter service"}
    };

    modulecmd_register_command(MXB_MODULE_NAME, "clear", MODULECMD_TYPE_ACTIVE,
                               schemarouter::SchemaRouter::clear_shards, 1, cmd_args,
                               "Clear schemarouter shard map cache");

    modulecmd_register_command(MXB_MODULE_NAME, "invalidate", MODULECMD_TYPE_ACTIVE,
                               schemarouter::SchemaRouter::invalidate_shards, 1, cmd_args,
                               "Invalidate schemarouter shard map cache");

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::BETA,
        MXS_ROUTER_VERSION,
        desc,
        "V1.0.0",
        schemarouter::CAPABILITIES,
        api_ptr,
        NULL,
        NULL,
        NULL,
        NULL,
        &s_spec
    };

    return &info;
}
