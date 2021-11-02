/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "schemarouter.hh"

#include <maxscale/utils.hh>

namespace schemarouter
{

Config::Config(mxs::ConfigParameters* conf)
    : refresh_min_interval(conf->get_duration<std::chrono::seconds>("refresh_interval").count())
    , refresh_databases(conf->get_bool("refresh_databases"))
    , debug(conf->get_bool("debug"))
    , ignore_regex(NULL)
    , ignore_match_data(NULL)
{
    // TODO: Don't process this in the router
    if (conf->contains(CN_IGNORE_TABLES_REGEX))
    {
        ignore_regex = conf->get_compiled_regex(CN_IGNORE_TABLES_REGEX, 0, NULL).release();
        ignore_match_data = pcre2_match_data_create_from_pattern(ignore_regex, NULL);
    }
    else if (conf->contains(CN_IGNORE_DATABASES_REGEX))
    {
        MXS_WARNING("Parameter '%s' has been deprecated, use '%s' instead.",
                    CN_IGNORE_DATABASES_REGEX, CN_IGNORE_TABLES_REGEX);
        ignore_regex = conf->get_compiled_regex(CN_IGNORE_DATABASES_REGEX, 0, NULL).release();
        ignore_match_data = pcre2_match_data_create_from_pattern(ignore_regex, NULL);
    }

    std::string ignored_dbs_str = conf->get_string(CN_IGNORE_TABLES);
    if (ignored_dbs_str.empty())
    {
        ignored_dbs_str = conf->get_string(CN_IGNORE_DATABASES);
        if (!ignored_dbs_str.empty())
        {
            MXS_WARNING("Parameter '%s' has been deprecated, use '%s' instead.",
                        CN_IGNORE_DATABASES, CN_IGNORE_TABLES);
        }
    }

    if (!ignored_dbs_str.empty())
    {
        for (const auto& a : mxs::strtok(ignored_dbs_str, ", \t"))
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
