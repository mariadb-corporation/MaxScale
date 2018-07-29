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
#include <string>
#include <set>
#include <vector>

#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/paths.h>
#include <maxscale/session.h>
#include <maxscale/spinlock.hh>
#include <maxscale/service.h>
#include <maxscale/filter.hh>
#include <maxscale/json_api.h>
#include <algorithm>

#include "internal/config.hh"
#include "internal/modules.h"
#include "internal/service.hh"

using std::string;
using std::set;

using namespace maxscale;

static SpinLock filter_spin;                /**< Protects the list of all filters */
static std::vector<FilterDef*>  allFilters; /**< The list of all filters */

/**
 * Free filter parameters
 * @param filter FilterDef whose parameters are to be freed
 */
static void filter_free_parameters(FilterDef* filter)
{
    config_parameter_free(filter->parameters);
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
FilterDef* filter_alloc(const char *name, const char *module, MXS_CONFIG_PARAMETER* params)
{
    MXS_FILTER_OBJECT* object = (MXS_FILTER_OBJECT*)load_module(module, MODULE_FILTER);

    if (object == NULL)
    {
        MXS_ERROR("Failed to load filter module '%s'.", module);
        return NULL;
    }

    char* my_name = MXS_STRDUP(name);
    char* my_module = MXS_STRDUP(module);

    FilterDef* filter = new (std::nothrow) FilterDef;

    if (!my_name || !my_module || !filter)
    {
        MXS_FREE(my_name);
        MXS_FREE(my_module);
        delete filter;
        return NULL;
    }
    filter->name = my_name;
    filter->module = my_module;
    filter->obj = object;
    filter->parameters = NULL;
    spinlock_init(&filter->spin);

    for (MXS_CONFIG_PARAMETER* p = params; p; p = p->next)
    {
        filter_add_parameter(filter, p->name, p->value);
    }

    if ((filter->filter = object->createInstance(name, params)) == NULL)
    {
        MXS_ERROR("Failed to create filter '%s' instance.", name);
        filter_free_parameters(filter);
        MXS_FREE(my_name);
        MXS_FREE(my_module);
        MXS_FREE(filter);
        return NULL;
    }

    filter_spin.acquire();
    allFilters.push_back(filter);
    filter_spin.release();

    return filter;
}

/**
 * Free the specified filter
 *
 * @param filter        The filter to free
 */
void filter_free(FilterDef* filter)
{
    if (filter)
    {
        /* First of all remove from the linked list */

        filter_spin.acquire();
        auto it = std::remove(allFilters.begin(), allFilters.end(), filter);
        allFilters.erase(it);
        filter_spin.release();

        /* Clean up session and free the memory */
        MXS_FREE(filter->name);
        MXS_FREE(filter->module);

        filter_free_parameters(filter);

        delete filter;
    }
}

FilterDef* filter_find(const char *name)
{
    FilterDef* rval = NULL;
    filter_spin.acquire();

    for (FilterDef* filter: allFilters)
    {
        if (strcmp(filter->name, name) == 0)
        {
            rval = filter;
            break;
        }
    }

    filter_spin.release();
    return rval;
}

MXS_FILTER_DEF* filter_def_find(const char *name)
{
    return filter_find(name);
}

bool filter_can_be_destroyed(MXS_FILTER_DEF *filter)
{
    return !service_filter_in_use(filter);
}

void filter_destroy(MXS_FILTER_DEF *filter)
{
    ss_dassert(filter_can_be_destroyed(filter));
    ss_info_dassert(!true, "Not yet implemented");
}

void filter_destroy_instances()
{
    filter_spin.acquire();

    for (FilterDef* filter: allFilters)
    {
        // NOTE: replace this with filter_destroy
        if (filter->obj->destroyInstance)
        {
            filter->obj->destroyInstance(filter->filter);
        }
    }

    filter_spin.release();
}

const char* filter_def_get_name(const MXS_FILTER_DEF* filter_def)
{
    const FilterDef* filter = static_cast<const FilterDef*>(filter_def);
    return filter->name;
}

const char* filter_def_get_module_name(const MXS_FILTER_DEF* filter_def)
{
    const FilterDef* filter = static_cast<const FilterDef*>(filter_def);
    return filter->module;
}

MXS_FILTER* filter_def_get_instance(const MXS_FILTER_DEF* filter_def)
{
    const FilterDef* filter = static_cast<const FilterDef*>(filter_def);
    return filter->filter;
}

/**
 * Check a parameter to see if it is a standard filter parameter
 *
 * @param name  Parameter name to check
 */
int
filter_standard_parameter(const char *name)
{
    if (strcmp(name, "type") == 0 || strcmp(name, "module") == 0)
    {
        return 1;
    }
    return 0;
}

/**
 * Print all filters to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active filters within MaxScale
 */
void
dprintAllFilters(DCB *dcb)
{
    filter_spin.acquire();

    for (FilterDef* ptr: allFilters)
    {
        dcb_printf(dcb, "FilterDef %p (%s)\n", ptr, ptr->name);
        dcb_printf(dcb, "\tModule:      %s\n", ptr->module);
        if (ptr->obj && ptr->filter)
        {
            ptr->obj->diagnostics(ptr->filter, NULL, dcb);
        }
        else
        {
            dcb_printf(dcb, "\tModule not loaded.\n");
        }
    }

    filter_spin.release();
}

/**
 * Print filter details to a DCB
 *
 * Designed to be called within a debug CLI in order
 * to display all active filters in MaxScale
 */
void dprintFilter(DCB *dcb, const FilterDef *filter)
{
    dcb_printf(dcb, "FilterDef %p (%s)\n", filter, filter->name);
    dcb_printf(dcb, "\tModule:      %s\n", filter->module);
    if (filter->obj && filter->filter)
    {
        filter->obj->diagnostics(filter->filter, NULL, dcb);
    }
}

/**
 * List all filters in a tabular form to a DCB
 *
 */
void
dListFilters(DCB *dcb)
{
    filter_spin.acquire();

    if (!allFilters.empty())
    {
        dcb_printf(dcb, "FilterDefs\n");
        dcb_printf(dcb, "--------------------+-----------------+----------------------------------------\n");
        dcb_printf(dcb, "%-19s | %-15s | Options\n",
                   "FilterDef", "Module");
        dcb_printf(dcb, "--------------------+-----------------+----------------------------------------\n");
    }

    for (FilterDef* ptr: allFilters)
    {
        dcb_printf(dcb, "%-19s | %-15s | ",
                   ptr->name, ptr->module);
        dcb_printf(dcb, "\n");
    }
    if (!allFilters.empty())
    {
        dcb_printf(dcb,
                   "--------------------+-----------------+----------------------------------------\n\n");
    }
    filter_spin.release();
}

/**
 * Add a router parameter to a service
 *
 * @param filter        The filter to add the parameter to
 * @param name          The parameter name
 * @param value         The parameter value
 */
void
filter_add_parameter(FilterDef *filter, const char *name, const char *value)
{
    CONFIG_CONTEXT ctx = {};
    ctx.object = (char*)"";

    config_add_param(&ctx, name, value);
    ctx.parameters->next = filter->parameters;
    filter->parameters = ctx.parameters;
}

/**
 * Connect the downstream filter chain for a filter.
 *
 * This will create the filter instance, loading the filter module, and
 * conenct the fitler into the downstream chain.
 *
 * @param filter        The filter to add into the chain
 * @param session       The client session
 * @param downstream    The filter downstream of this filter
 * @return              The downstream component for the next filter or NULL
 *                      if the filter could not be created
 */
MXS_DOWNSTREAM* filter_apply(FilterDef* filter, MXS_SESSION *session, MXS_DOWNSTREAM *downstream)
{
    MXS_DOWNSTREAM *me;

    if ((me = (MXS_DOWNSTREAM *)MXS_CALLOC(1, sizeof(MXS_DOWNSTREAM))) == NULL)
    {
        return NULL;
    }
    me->instance = filter->filter;
    me->routeQuery = filter->obj->routeQuery;

    if ((me->session = filter->obj->newSession(me->instance, session)) == NULL)
    {
        MXS_FREE(me);
        return NULL;
    }
    filter->obj->setDownstream(me->instance, me->session, downstream);

    return me;
}

/**
 * Connect a filter in the up stream filter chain for a session
 *
 * Note, the filter will have been created when the downstream chian was
 * previously setup.
 * Note all filters require to be in the upstream chain, so this routine
 * may skip a filter if it does not provide an upstream interface.
 *
 * @param filter        The fitler to add to the chain
 * @param fsession      The filter session
 * @param upstream      The filter that should be upstream of this filter
 * @return              The upstream component for the next filter
 */
MXS_UPSTREAM* filter_upstream(FilterDef* filter, MXS_FILTER_SESSION *fsession, MXS_UPSTREAM *upstream)
{
    MXS_UPSTREAM *me = NULL;

    /*
     * The the filter has no setUpstream entry point then is does
     * not require to see results and can be left out of the chain.
     */
    if (filter->obj->setUpstream == NULL)
    {
        return upstream;
    }

    if (filter->obj->clientReply != NULL)
    {
        if ((me = (MXS_UPSTREAM *)MXS_CALLOC(1, sizeof(MXS_UPSTREAM))) == NULL)
        {
            return NULL;
        }
        me->instance = filter->filter;
        me->session = fsession;
        me->clientReply = filter->obj->clientReply;
        filter->obj->setUpstream(me->instance, me->session, upstream);
    }
    return me;
}
json_t* filter_parameters_to_json(const FilterDef* filter)
{
    json_t* rval = json_object();

    /** Add custom module parameters */
    const MXS_MODULE* mod = get_module(filter->module, MODULE_FILTER);
    config_add_module_params_json(mod, filter->parameters, config_filter_params, rval);

    return rval;
}

json_t* filter_json_data(const FilterDef* filter, const char* host)
{
    json_t* rval = json_object();

    json_object_set_new(rval, CN_ID, json_string(filter->name));
    json_object_set_new(rval, CN_TYPE, json_string(CN_FILTERS));

    json_t* attr = json_object();

    json_object_set_new(attr, CN_MODULE, json_string(filter->module));
    json_object_set_new(attr, CN_PARAMETERS, filter_parameters_to_json(filter));

    if (filter->obj && filter->filter && filter->obj->diagnostics_json)
    {
        json_t* diag = filter->obj->diagnostics_json(filter->filter, NULL);

        if (diag)
        {
            json_object_set_new(attr, CN_FILTER_DIAGNOSTICS, diag);
        }
    }

    /** Store relationships to other objects */
    json_t* rel = json_object();
    json_object_set_new(rel, CN_SERVICES, service_relations_to_filter(filter, host));

    json_object_set_new(rval, CN_RELATIONSHIPS, rel);
    json_object_set_new(rval, CN_ATTRIBUTES, attr);
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_FILTERS, filter->name));

    return rval;
}

json_t* filter_to_json(const FilterDef* filter, const char* host)
{
    string self = MXS_JSON_API_FILTERS;
    self += filter->name;
    return mxs_json_resource(host, self.c_str(), filter_json_data(filter, host));
}

json_t* filter_list_to_json(const char* host)
{
    json_t* rval = json_array();

    filter_spin.acquire();

    for (FilterDef* f: allFilters)
    {
        json_t* json = filter_json_data(f, host);

        if (json)
        {
            json_array_append_new(rval, json);
        }
    }

    filter_spin.release();

    return mxs_json_resource(host, MXS_JSON_API_FILTERS, rval);
}

namespace maxscale
{

//
// FilterSession
//

FilterSession::FilterSession(MXS_SESSION* pSession)
    : m_pSession(pSession)
{
}

FilterSession::~FilterSession()
{
}

void FilterSession::close()
{
}

void FilterSession::setDownstream(const Downstream& down)
{
    m_down = down;
}

void FilterSession::setUpstream(const Upstream& up)
{
    m_up = up;
}

int FilterSession::routeQuery(GWBUF* pPacket)
{
    return m_down.routeQuery(pPacket);
}

int FilterSession::clientReply(GWBUF* pPacket)
{
    return m_up.clientReply(pPacket);
}

void FilterSession::diagnostics(DCB *pDcb)
{
}

json_t* FilterSession::diagnostics_json() const
{
    return NULL;
}

}

static bool create_filter_config(const FilterDef *filter, const char *filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing filter '%s': %d, %s",
                  filename, filter->name, errno, mxs_strerror(errno));
        return false;
    }

    mxs::SpinLockGuard guard(filter->spin);

    dprintf(file, "[%s]\n", filter->name);
    dprintf(file, "%s=%s\n", CN_TYPE, CN_FILTER);
    dprintf(file, "%s=%s\n", CN_MODULE, filter->module);

    std::set<std::string> param_set{CN_TYPE, CN_MODULE};

    for (MXS_CONFIG_PARAMETER* p = filter->parameters; p; p = p->next)
    {
        if (param_set.count(p->name) == 0)
        {
            dprintf(file, "%s=%s\n", p->name, p->value);
        }
    }

    close(file);

    return true;
}

bool filter_serialize(const FilterDef *filter)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf.tmp", get_config_persistdir(),
             filter->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary filter configuration at '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }
    else if (create_filter_config(filter, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char *dot = strrchr(final_filename, '.');
        ss_dassert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary filter configuration at '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
    }

    return rval;
}
