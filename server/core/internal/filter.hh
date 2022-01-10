/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file core/maxscale/filter.h - The private filter interface
 */

#include <maxscale/filter.hh>

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
              mxs::ConfigParameters* params);
    ~FilterDef();

    std::string           name;         /**< The Filter name */
    std::string           module;       /**< The module to load */
    mxs::ConfigParameters parameters;   /**< The filter parameters */
    MXS_FILTER*           filter;       /**< The runtime filter */
    MXS_FILTER_OBJECT*    obj;          /**< The "MODULE_OBJECT" for the filter */
};

typedef std::shared_ptr<FilterDef> SFilterDef;

SFilterDef filter_alloc(const char* name, const char* module, mxs::ConfigParameters* params);
void       filter_free(const SFilterDef& filter);
int        filter_standard_parameter(const char* name);

// Find the internal filter representation
SFilterDef filter_find(const char* name);

/**
 * Check if a filter uses a server or a service
 *
 * @param target The target to check
 *
 * @return The list of filters that depend on the given target
 */
std::vector<SFilterDef> filter_depends_on_target(const mxs::Target* target);

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
 * @brief Persist filter configuration into a stream
 *
 * This converts the static configuration of the filter into an INI format file.
 *
 * @param filter Filter to persist
 * @param os     Stream where filter is serialized
 *
 * @return The output stream
 */
std::ostream& filter_persist(const SFilterDef& filter, std::ostream& os);

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
