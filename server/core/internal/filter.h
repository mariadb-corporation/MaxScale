#pragma once
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
 * @file core/maxscale/filter.h - The private filter interface
 */

#include <maxscale/filter.h>

MXS_BEGIN_DECLS

/**
 * The definition of a filter from the configuration file.
 * This is basically the link between a plugin to load and the
 * options to pass to that plugin.
 */
struct mxs_filter_def
{
    char *name;                   /**< The Filter name */
    char *module;                 /**< The module to load */
    MXS_CONFIG_PARAMETER *parameters; /**< The filter parameters */
    MXS_FILTER* filter;           /**< The runtime filter */
    MXS_FILTER_OBJECT *obj;       /**< The "MODULE_OBJECT" for the filter */
    SPINLOCK spin;                /**< Spinlock to protect the filter definition */
    struct mxs_filter_def *next;  /**< Next filter in the chain of all filters */
};

void filter_add_parameter(MXS_FILTER_DEF *filter_def, const char *name, const char *value);
MXS_FILTER_DEF *filter_alloc(const char *name, const char *module, MXS_CONFIG_PARAMETER* params);
MXS_DOWNSTREAM *filter_apply(MXS_FILTER_DEF *filter_def, MXS_SESSION *session, MXS_DOWNSTREAM *downstream);
void filter_free(MXS_FILTER_DEF *filter_def);
int filter_standard_parameter(const char *name);
MXS_UPSTREAM *filter_upstream(MXS_FILTER_DEF *filter_def,
                              MXS_FILTER_SESSION *fsession,
                              MXS_UPSTREAM *upstream);

/**
 * Check if filter can be destroyed
 *
 * A filter can be destroyed if no service uses it.
 *
 * @param filter Filter to check
 *
 * @return True if filter can be destroyed
 */
bool filter_can_be_destroyed(MXS_FILTER_DEF *filter);

/**
 * Destroy a filter
 *
 * @param filter Filter to destroy
 */
void filter_destroy(MXS_FILTER_DEF *filter);

/**
 * Destroy all filters
 */
void filter_destroy_instances();

MXS_END_DECLS
