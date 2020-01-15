/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-01-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "schemarouter.hh"

#include <maxscale/utils.hh>

namespace schemarouter
{

Config::Config(MXS_CONFIG_PARAMETER* conf)
    : refresh_min_interval(config_get_integer(conf, "refresh_interval"))
    , refresh_databases(config_get_bool(conf, "refresh_databases"))
    , debug(config_get_bool(conf, "debug"))
    , ignore_regex(config_get_compiled_regex(conf, "ignore_databases_regex", 0, NULL))
    , ignore_match_data(ignore_regex ? pcre2_match_data_create_from_pattern(ignore_regex, NULL) : NULL)
    , preferred_server(config_get_server(conf, "preferred_server"))
{
    // TODO: Don't process this in the router
    if (MXS_CONFIG_PARAMETER* p = config_get_param(conf, "ignore_databases"))
    {
        for (const auto& a : mxs::strtok(p->value, ", \t"))
        {
            ignored_tables.insert(a);
        }
    }
}

void SRBackend::set_mapped(bool value)
{
    m_mapped = value;
}

bool SRBackend::is_mapped() const
{
    return m_mapped;
}
}
