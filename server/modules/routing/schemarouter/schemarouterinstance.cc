/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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

std::shared_ptr<mxs::RouterSession> SchemaRouter::newSession(MXS_SESSION* pSession, const Endpoints& endpoints)
{
    SRBackendList backends;

    for (auto e : endpoints)
    {
        auto b = std::make_unique<SRBackend>(e);

        if (b->can_connect() && b->connect())
        {
            MXB_INFO("Connected %s in '%s'", b->target()->status_string().c_str(), b->name());
            backends.push_back(std::move(b));
        }
    }

    if (backends.empty())
    {
        MXB_ERROR("Failed to connect to any of the backend servers");
        return nullptr;
    }

    return std::make_shared<SchemaRouterSession>(pSession, this, std::move(backends));
}

json_t* SchemaRouter::diagnostics() const
{
    json_t* rval = json_object();

    auto stats = m_shard_manager.stats();
    json_object_set_new(rval, "shard_map_hits", json_integer(stats.hits));
    json_object_set_new(rval, "shard_map_misses", json_integer(stats.misses));
    json_object_set_new(rval, "shard_map_updates", json_integer(stats.updates));
    json_object_set_new(rval, "shard_map_stale", json_integer(stats.stale));

    return rval;
}

static const uint64_t CAPABILITIES = RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RUNTIME_CONFIG
    | RCAP_TYPE_REQUEST_TRACKING | RCAP_TYPE_QUERY_CLASSIFICATION | RCAP_TYPE_SESCMD_HISTORY
    | RCAP_TYPE_OLD_PROTOCOL | RCAP_TYPE_AUTH_ALL_SERVERS;

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
        mxs::ModuleStatus::GA,
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
