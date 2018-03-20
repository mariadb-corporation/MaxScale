/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file filter.c  - A representation of a filter within MaxScale.
 */

#include "internal/filter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <set>

#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/session.h>
#include <maxscale/spinlock.h>
#include <maxscale/service.h>
#include <maxscale/filter.hh>
#include <maxscale/json_api.h>

#include "internal/config.h"
#include "internal/modules.h"

using std::string;
using std::set;

static SPINLOCK filter_spin = SPINLOCK_INIT;    /**< Protects the list of all filters */
static MXS_FILTER_DEF *allFilters = NULL;           /**< The list of all filters */

static void filter_free_parameters(MXS_FILTER_DEF *filter);

/**
 * Allocate a new filter within MaxScale
 *
 *
 * @param name          The filter name
 * @param module        The module to load
 *
 * @return              The newly created filter or NULL if an error occured
 */
MXS_FILTER_DEF *
filter_alloc(const char *name, const char *module)
{
    char* my_name = MXS_STRDUP(name);
    char* my_module = MXS_STRDUP(module);

    MXS_FILTER_DEF *filter = (MXS_FILTER_DEF *)MXS_MALLOC(sizeof(MXS_FILTER_DEF));

    if (!my_name || !my_module || !filter)
    {
        MXS_FREE(my_name);
        MXS_FREE(my_module);
        MXS_FREE(filter);
        return NULL;
    }
    filter->name = my_name;
    filter->module = my_module;
    filter->filter = NULL;
    filter->options = NULL;
    filter->obj = NULL;
    filter->parameters = NULL;

    spinlock_init(&filter->spin);

    spinlock_acquire(&filter_spin);
    filter->next = allFilters;
    allFilters = filter;
    spinlock_release(&filter_spin);

    return filter;
}


/**
 * Deallocate the specified filter
 *
 * @param filter        The filter to deallocate
 * @return      Returns true if the server was freed
 */
void
filter_free(MXS_FILTER_DEF *filter)
{
    MXS_FILTER_DEF *ptr;

    if (filter)
    {
        /* First of all remove from the linked list */
        spinlock_acquire(&filter_spin);
        if (allFilters == filter)
        {
            allFilters = filter->next;
        }
        else
        {
            ptr = allFilters;
            while (ptr && ptr->next != filter)
            {
                ptr = ptr->next;
            }
            if (ptr)
            {
                ptr->next = filter->next;
            }
        }
        spinlock_release(&filter_spin);

        /* Clean up session and free the memory */
        MXS_FREE(filter->name);
        MXS_FREE(filter->module);

        if (filter->options)
        {
            for (int i = 0; filter->options[i]; i++)
            {
                MXS_FREE(filter->options[i]);
            }
            MXS_FREE(filter->options);
        }

        filter_free_parameters(filter);

        MXS_FREE(filter);
    }
}

MXS_FILTER_DEF *
filter_def_find(const char *name)
{
    MXS_FILTER_DEF *filter;

    spinlock_acquire(&filter_spin);
    filter = allFilters;
    while (filter)
    {
        if (strcmp(filter->name, name) == 0)
        {
            break;
        }
        filter = filter->next;
    }
    spinlock_release(&filter_spin);
    return filter;
}

const char* filter_def_get_name(const MXS_FILTER_DEF* filter_def)
{
    return filter_def->name;
}

const char* filter_def_get_module_name(const MXS_FILTER_DEF* filter_def)
{
    return filter_def->module;
}

MXS_FILTER* filter_def_get_instance(const MXS_FILTER_DEF* filter_def)
{
    return filter_def->filter;
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
    MXS_FILTER_DEF *ptr;
    int        i;

    spinlock_acquire(&filter_spin);
    ptr = allFilters;
    while (ptr)
    {
        dcb_printf(dcb, "Filter %p (%s)\n", ptr, ptr->name);
        dcb_printf(dcb, "\tModule:      %s\n", ptr->module);
        if (ptr->options)
        {
            dcb_printf(dcb, "\tOptions:     ");
            for (i = 0; ptr->options && ptr->options[i]; i++)
            {
                dcb_printf(dcb, "%s ", ptr->options[i]);
            }
            dcb_printf(dcb, "\n");
        }
        if (ptr->obj && ptr->filter)
        {
            ptr->obj->diagnostics(ptr->filter, NULL, dcb);
        }
        else
        {
            dcb_printf(dcb, "\tModule not loaded.\n");
        }
        ptr = ptr->next;
    }
    spinlock_release(&filter_spin);
}

/**
 * Print filter details to a DCB
 *
 * Designed to be called within a debug CLI in order
 * to display all active filters in MaxScale
 */
void
dprintFilter(DCB *dcb, const MXS_FILTER_DEF *filter)
{
    int i;

    dcb_printf(dcb, "Filter %p (%s)\n", filter, filter->name);
    dcb_printf(dcb, "\tModule:      %s\n", filter->module);
    if (filter->options)
    {
        dcb_printf(dcb, "\tOptions:     ");
        for (i = 0; filter->options && filter->options[i]; i++)
        {
            dcb_printf(dcb, "%s ", filter->options[i]);
        }
        dcb_printf(dcb, "\n");
    }
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
    MXS_FILTER_DEF      *ptr;
    int     i;

    spinlock_acquire(&filter_spin);
    ptr = allFilters;
    if (ptr)
    {
        dcb_printf(dcb, "Filters\n");
        dcb_printf(dcb, "--------------------+-----------------+----------------------------------------\n");
        dcb_printf(dcb, "%-19s | %-15s | Options\n",
                   "Filter", "Module");
        dcb_printf(dcb, "--------------------+-----------------+----------------------------------------\n");
    }
    while (ptr)
    {
        dcb_printf(dcb, "%-19s | %-15s | ",
                   ptr->name, ptr->module);
        for (i = 0; ptr->options && ptr->options[i]; i++)
        {
            dcb_printf(dcb, "%s ", ptr->options[i]);
        }
        dcb_printf(dcb, "\n");
        ptr = ptr->next;
    }
    if (allFilters)
    {
        dcb_printf(dcb,
                   "--------------------+-----------------+----------------------------------------\n\n");
    }
    spinlock_release(&filter_spin);
}

/**
 * Add a router option to a service
 *
 * @param filter        The filter to add the option to
 * @param option        The option string
 */
void
filter_add_option(MXS_FILTER_DEF *filter, const char *option)
{
    int i;

    spinlock_acquire(&filter->spin);
    if (filter->options == NULL)
    {
        filter->options = (char **)MXS_CALLOC(2, sizeof(char *));
        MXS_ABORT_IF_NULL(filter->options);
        filter->options[0] = MXS_STRDUP_A(option);
        filter->options[1] = NULL;
    }
    else
    {
        for (i = 0; filter->options[i]; i++)
        {
            ;
        }

        filter->options = (char **)MXS_REALLOC(filter->options, (i + 2) * sizeof(char *));
        MXS_ABORT_IF_NULL(filter->options);
        filter->options[i] = MXS_STRDUP_A(option);
        MXS_ABORT_IF_NULL(filter->options[i]);
        filter->options[i + 1] = NULL;
    }
    spinlock_release(&filter->spin);
}

/**
 * Add a router parameter to a service
 *
 * @param filter        The filter to add the parameter to
 * @param name          The parameter name
 * @param value         The parameter value
 */
void
filter_add_parameter(MXS_FILTER_DEF *filter, const char *name, const char *value)
{
    CONFIG_CONTEXT ctx = {};
    ctx.object = (char*)"";

    config_add_param(&ctx, name, value);
    ctx.parameters->next = filter->parameters;
    filter->parameters = ctx.parameters;
}

/**
 * Free filter parameters
 * @param filter Filter whose parameters are to be freed
 */
static void filter_free_parameters(MXS_FILTER_DEF *filter)
{
    config_parameter_free(filter->parameters);
}

/**
 * Load a filter module for use and create an instance of it for a service.
 * @param filter Filter definition
 * @return True if module was successfully loaded, false if an error occurred
 */
bool filter_load(MXS_FILTER_DEF* filter)
{
    bool rval = false;
    if (filter)
    {
        if (filter->filter)
        {
            // Already loaded and created.
            rval = true;
        }
        else
        {
            if (filter->obj == NULL)
            {
                /* Filter not yet loaded */
                if ((filter->obj = (MXS_FILTER_OBJECT*)load_module(filter->module, MODULE_FILTER)) == NULL)
                {
                    MXS_ERROR("Failed to load filter module '%s'.", filter->module);
                }
            }

            if (filter->obj)
            {
                ss_dassert(!filter->filter);

                if ((filter->filter = (filter->obj->createInstance)(filter->name,
                                                                    filter->options,
                                                                    filter->parameters)))
                {
                    rval = true;
                }
                else
                {
                    MXS_ERROR("Failed to create filter '%s' instance.", filter->name);
                }
            }
        }
    }
    return rval;
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
MXS_DOWNSTREAM *
filter_apply(MXS_FILTER_DEF *filter, MXS_SESSION *session, MXS_DOWNSTREAM *downstream)
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
MXS_UPSTREAM *
filter_upstream(MXS_FILTER_DEF *filter, MXS_FILTER_SESSION *fsession, MXS_UPSTREAM *upstream)
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
json_t* filter_parameters_to_json(const MXS_FILTER_DEF* filter)
{
    json_t* rval = json_object();

    if (filter->options)
    {
        json_t* arr = json_array();

        for (int i = 0; filter->options && filter->options[i]; i++)
        {
            json_array_append_new(arr, json_string(filter->options[i]));
        }

        json_object_set_new(rval, "options", arr);
    }

    /** Add custom module parameters */
    const MXS_MODULE* mod = get_module(filter->module, MODULE_FILTER);
    config_add_module_params_json(mod, filter->parameters, config_filter_params, rval);

    return rval;
}

json_t* filter_json_data(const MXS_FILTER_DEF* filter, const char* host)
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

json_t* filter_to_json(const MXS_FILTER_DEF* filter, const char* host)
{
    string self = MXS_JSON_API_FILTERS;
    self += filter->name;
    return mxs_json_resource(host, self.c_str(), filter_json_data(filter, host));
}

json_t* filter_list_to_json(const char* host)
{
    json_t* rval = json_array();

    spinlock_acquire(&filter_spin);

    for (MXS_FILTER_DEF* f = allFilters; f; f = f->next)
    {
        json_t* json = filter_json_data(f, host);

        if (json)
        {
            json_array_append_new(rval, json);
        }
    }

    spinlock_release(&filter_spin);

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
