/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "binlogfilter"

#include "binlogconfig.hh"

namespace
{
namespace cfg = mxs::config;

class BinlogfilterSpecification : public cfg::Specification
{
    using cfg::Specification::Specification;

    template<class Params>
    bool do_post_validate(Params params) const;

    bool post_validate(const mxs::ConfigParameters& params) const override
    {
        return do_post_validate(params);
    }

    bool post_validate(json_t* json) const override
    {
        return do_post_validate(json);
    }
};

BinlogfilterSpecification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamRegex s_match(
    &s_spec, "match", "Only process events from tables matching this pattern", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_exclude(
    &s_spec, "exclude", "Exclude events from tables matching this pattern", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_rewrite_src(
    &s_spec, REWRITE_SRC, "Pattern used for query replacement", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_rewrite_dest(
    &s_spec, REWRITE_DEST, "Replacement value for query replacement regex", "",
    cfg::Param::AT_RUNTIME);

template<class Params>
bool BinlogfilterSpecification::do_post_validate(Params params) const
{
    bool rv = s_rewrite_src.get(params).empty() == s_rewrite_dest.get(params).empty();

    if (!rv)
    {
        MXS_ERROR("Both '%s' and '%s' must be defined", REWRITE_SRC, REWRITE_DEST);
    }

    return rv;
}
}

// static
mxs::config::Specification* BinlogConfig::specification()
{
    return &s_spec;
}

BinlogConfig::BinlogConfig(const char* name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&BinlogConfig::m_v, &Values::match, &s_match);
    add_native(&BinlogConfig::m_v, &Values::exclude, &s_exclude);
    add_native(&BinlogConfig::m_v, &Values::rewrite_src, &s_rewrite_src);
    add_native(&BinlogConfig::m_v, &Values::rewrite_dest, &s_rewrite_dest);
}
