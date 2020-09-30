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

/**
 * @file filter.c  - A representation of a filter within MaxScale.
 */

#include "internal/filter.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <maxbase/alloc.h>
#include <maxscale/paths.hh>
#include <maxscale/session.hh>
#include <maxscale/service.hh>
#include <maxscale/filter.hh>
#include <maxscale/json_api.hh>

#include "internal/config.hh"
#include "internal/modules.hh"
#include "internal/service.hh"

using std::string;
using std::set;
using Guard = std::lock_guard<std::mutex>;

using namespace maxscale;

static struct
{
    std::mutex              lock;
    std::vector<SFilterDef> filters;
} this_unit;

static const MXS_MODULE_PARAM config_filter_params[] =
{
    {
        CN_TYPE, MXS_MODULE_PARAM_STRING,
        CN_FILTER,
        MXS_MODULE_OPT_REQUIRED
    },
    {
        CN_MODULE,
        MXS_MODULE_PARAM_STRING,
        NULL,
        MXS_MODULE_OPT_REQUIRED
    },
    {NULL}
};

const MXS_MODULE_PARAM* common_filter_params()
{
    return config_filter_params;
}

/**
 * Allocate a new filter
 *
 * @param name   The filter name
 * @param module The module to load
 * @param params Module parameters
 *
 * @return The newly created filter or NULL if an error occurred
 */
SFilterDef filter_alloc(const char* name, const char* module, mxs::ConfigParameters* params)
{
    SFilterDef filter;
    auto module_info = get_module(module, ModuleType::FILTER);
    if (module_info)
    {
        auto func = (mxs::FILTER_API*)module_info->module_object;
        auto instance = func->createInstance(name, params);
        if (instance)
        {
            filter.reset(new(std::nothrow) FilterDef(name, module, instance, *params));
            if (filter)
            {
                if (auto config = filter->configuration())
                {
                    if (!config->configure(*params))
                    {
                        filter.reset();
                    }
                }
            }

            if (filter)
            {
                Guard guard(this_unit.lock);
                this_unit.filters.push_back(filter);
            }
            else
            {
                delete instance;
            }
        }
        else
        {
            MXB_ERROR("Failed to create filter '%s' instance.", name);
        }
    }
    return filter;
}

FilterDef::FilterDef(std::string name, std::string module, Filter* instance,
                     mxs::ConfigParameters params)
    : m_name(std::move(name))
    , m_module(std::move(module))
    , m_parameters(std::move(params))
    , m_filter(instance)
    , m_capabilities(m_filter->getCapabilities())
{
    mxb_assert(get_module(m_module.c_str(), MODULE_FILTER)->module_capabilities == m_capabilities);
}

FilterDef::~FilterDef()
{
    MXS_INFO("Destroying '%s'", name());
    delete m_filter;
}

/**
 * Free the specified filter
 *
 * @param filter        The filter to free
 */
void filter_free(const SFilterDef& filter)
{
    mxb_assert(filter);
    // Removing the filter from the list will trigger deletion once it's no longer in use
    Guard guard(this_unit.lock);
    auto it = std::remove(this_unit.filters.begin(), this_unit.filters.end(), filter);
    mxb_assert(it != this_unit.filters.end());
    this_unit.filters.erase(it);
}

SFilterDef filter_find(const std::string& name)
{
    Guard guard(this_unit.lock);

    for (const auto& filter : this_unit.filters)
    {
        if (filter->name() == name)
        {
            return filter;
        }
    }

    return SFilterDef();
}

bool filter_can_be_destroyed(const SFilterDef& filter)
{
    mxb_assert(filter);
    return service_filter_in_use(filter).empty();
}

void filter_destroy(const SFilterDef& filter)
{
    mxb_assert(filter);
    mxb_assert(filter_can_be_destroyed(filter));
    filter_free(filter);
}

void filter_destroy_instances()
{
    Guard guard(this_unit.lock);
    this_unit.filters.clear();
}

Filter* filter_def_get_instance(const MXS_FILTER_DEF* filter_def)
{
    const FilterDef* filter = static_cast<const FilterDef*>(filter_def);
    mxb_assert(filter);
    return filter->instance();
}

json_t* FilterDef::parameters_to_json() const
{
    json_t* rval = json_object();

    /** Add custom module parameters */
    const MXS_MODULE* mod = get_module(module(), MODULE_FILTER);
    config_add_module_params_json(parameters(),
                                  {CN_TYPE, CN_MODULE},
                                  common_filter_params(),
                                  mod->parameters,
                                  rval);

    if (auto cfg = m_filter->getConfiguration())
    {
        auto json = cfg->to_json();
        json_object_update(rval, json);
        json_decref(json);
    }

    return rval;
}

json_t* FilterDef::json_data(const char* host) const
{
    const char CN_FILTER_DIAGNOSTICS[] = "filter_diagnostics";
    json_t* rval = json_object();

    json_object_set_new(rval, CN_ID, json_string(name()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_FILTERS));

    json_t* attr = json_object();

    json_object_set_new(attr, CN_MODULE, json_string(module()));
    json_object_set_new(attr, CN_PARAMETERS, parameters_to_json());

    if (json_t* diag = instance()->diagnostics())
    {
        json_object_set_new(attr, CN_FILTER_DIAGNOSTICS, diag);
    }

    /** Store relationships to other objects */
    json_t* rel = json_object();

    std::string self = MXS_JSON_API_FILTERS + m_name + "/relationships/services";

    if (auto services = service_relations_to_filter(this, host, self))
    {
        json_object_set_new(rel, CN_SERVICES, services);
    }

    json_object_set_new(rval, CN_RELATIONSHIPS, rel);
    json_object_set_new(rval, CN_ATTRIBUTES, attr);
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_FILTERS, name()));

    return rval;
}

json_t* FilterDef::to_json(const char* host) const
{
    string self = MXS_JSON_API_FILTERS + m_name;
    return mxs_json_resource(host, self.c_str(), json_data(host));
}

// static
json_t* FilterDef::filter_list_to_json(const char* host)
{
    json_t* rval = json_array();

    Guard guard(this_unit.lock);

    for (const auto& f : this_unit.filters)
    {
        if (json_t* json = f->json_data(host))
        {
            json_array_append_new(rval, json);
        }
    }

    return mxs_json_resource(host, MXS_JSON_API_FILTERS, rval);
}

namespace maxscale
{

//
// FilterSession
//

FilterSession::FilterSession(MXS_SESSION* pSession, SERVICE* pService)
    : m_pSession(pSession)
    , m_pService(pService)
{
}

FilterSession::~FilterSession()
{
}

void FilterSession::setDownstream(mxs::Routable* down)
{
    m_down = down;
}

void FilterSession::setUpstream(mxs::Routable* up)
{
    m_up = up;
}

int FilterSession::routeQuery(GWBUF* pPacket)
{
    return m_down->routeQuery(pPacket);
}

int FilterSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return m_up->clientReply(pPacket, down, reply);
}

json_t* FilterSession::diagnostics() const
{
    return NULL;
}

void FilterSession::set_response(GWBUF* pResponse) const
{
    session_set_response(m_pSession, m_pService, m_up, pResponse);
}
}

std::ostream& FilterDef::persist(std::ostream& os) const
{
    if (auto config = configuration())
    {
        config->persist(os);
        os << "type=filter\n";
        os << "module=" << module() << "\n";
    }
    else
    {
        const MXS_MODULE* mod = get_module(module(), NULL);
        mxb_assert(mod);

        os << generate_config_string(name(), parameters(),
                                     common_filter_params(),
                                     mod->parameters);
    }

    return os;
}
