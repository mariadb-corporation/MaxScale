/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file core/maxscale/filter.h - The private filter interface
 */

#include <maxscale/filter.h>

#include <memory>
#include <mutex>

/**
 * The definition of a filter from the configuration file.
 * This is basically the link between a plugin to load and the
 * options to pass to that plugin.
 */
// TODO: Make this a class
struct FilterDef : public MXS_FILTER_DEF
{
    FilterDef(std::string name,
              std::string module,
              MXS_FILTER_OBJECT* object,
              MXS_FILTER* instance,
              MXS_CONFIG_PARAMETER* params);
    ~FilterDef();

    std::string           name;         /**< The Filter name */
    std::string           module;       /**< The module to load */
    MXS_CONFIG_PARAMETER* parameters;   /**< The filter parameters */
    MXS_FILTER*           filter;       /**< The runtime filter */
    MXS_FILTER_OBJECT*    obj;          /**< The "MODULE_OBJECT" for the filter */
    mutable std::mutex    lock;
};

typedef std::shared_ptr<FilterDef> SFilterDef;

void            filter_add_parameter(SFilterDef& filter_def, const char* name, const char* value);
SFilterDef      filter_alloc(const char* name, const char* module, MXS_CONFIG_PARAMETER* params);
MXS_DOWNSTREAM* filter_apply(const SFilterDef& filter_def, MXS_SESSION* session, MXS_DOWNSTREAM* downstream);
void            filter_free(const SFilterDef& filter);
int             filter_standard_parameter(const char* name);
MXS_UPSTREAM*   filter_upstream(const SFilterDef&   filter_def,
                                MXS_FILTER_SESSION* fsession,
                                MXS_UPSTREAM* upstream);

// Find the internal filter representation
SFilterDef filter_find(const char* name);

/**
 * Check if filter can be destroyed
 *
 * A filter can be destroyed if no service uses it.
 *
 * @param filter Filter to check
 *
 * @return True if filter can be destroyed
 */
bool filter_can_be_destroyed(const SFilterDef& filter);

/**
 * Destroy a filter
 *
 * @param filter Filter to destroy
 */
void filter_destroy(const SFilterDef& filter);

/**
 * Destroy all filters
 */
void filter_destroy_instances();

/**
 * @brief Serialize a filter to a file
 *
 * This converts the static configuration of the filter into an INI format file.
 *
 * @param filter Monitor to serialize
 *
 * @return True if serialization was successful
 */
bool filter_serialize(const SFilterDef& filter);

void dprintAllFilters(DCB*);
void dprintFilter(DCB*, const FilterDef*);
void dListFilters(DCB*);

/**
 * @brief Convert a filter to JSON
 *
 * @param filter Filter to convert
 * @param host Hostname of this server
 *
 * @return Filter converted to JSON format
 */
json_t* filter_to_json(const SFilterDef& filter, const char* host);

/**
 * @brief Convert all filters into JSON
 *
 * @param host Hostname of this server
 *
 * @return A JSON array containing all filters
 */
json_t* filter_list_to_json(const char* host);
