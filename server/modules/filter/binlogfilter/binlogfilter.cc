/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "binlogfilter"

#include "binlogfilter.hh"

namespace
{
namespace cfg = mxs::config;

class BinlogfilterSpecification : public cfg::Specification
{
    using cfg::Specification::Specification;

    bool post_validate(const mxs::ConfigParameters& params) const override
    {
        bool rv = params.get_string(REWRITE_SRC).empty() == params.get_string(REWRITE_DEST).empty();

        if (!rv)
        {
            MXS_ERROR("Both '%s' and '%s' must be defined", REWRITE_SRC, REWRITE_DEST);
        }

        return rv;
    }

    bool post_validate(json_t* json) const override
    {
        auto rewrite_src = json_object_get(json, REWRITE_SRC);
        auto rewrite_dest = json_object_get(json, REWRITE_DEST);
        bool rv = json_is_string(rewrite_src) == json_is_string(rewrite_src);

        if (!rv)
        {
            MXS_ERROR("Both '%s' and '%s' must be defined", REWRITE_SRC, REWRITE_DEST);
        }

        return rv;
    }
};

BinlogfilterSpecification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamRegex s_match(
    &s_spec, "match", "Only process events from tables matching this pattern", "",
    cfg::Param::AT_STARTUP);

cfg::ParamRegex s_exclude(
    &s_spec, "exclude", "Exclude events from tables matching this pattern", "",
    cfg::Param::AT_STARTUP);

cfg::ParamRegex s_rewrite_src(
    &s_spec, REWRITE_SRC, "Pattern used for query replacement", "",
    cfg::Param::AT_STARTUP);

cfg::ParamString s_rewrite_dest(
    &s_spec, REWRITE_DEST, "Replacement value for query replacement regex", "",
    cfg::Param::AT_STARTUP);
}

// This declares a module in MaxScale
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char desc[] = "A binlog event filter for slave servers";
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        desc,
        "V1.0.0",
        RCAP_TYPE_STMT_OUTPUT,
        &BinlogFilter::s_object,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        &s_spec
    };

    return &info;
}

BinlogConfig::BinlogConfig(const char* name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&BinlogConfig::match, &s_match);
    add_native(&BinlogConfig::exclude, &s_exclude);
    add_native(&BinlogConfig::rewrite_src, &s_rewrite_src);
    add_native(&BinlogConfig::rewrite_dest, &s_rewrite_dest);
}

// BinlogFilter constructor
BinlogFilter::BinlogFilter(const char* name)
    : m_config(name)
{
}

// BinlogFilter destructor
BinlogFilter::~BinlogFilter()
{
}

// static: filter create routine
BinlogFilter* BinlogFilter::create(const char* zName,
                                   mxs::ConfigParameters* pParams)
{
    return new BinlogFilter(zName);
}

// BinlogFilterSession create routine
BinlogFilterSession* BinlogFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return BinlogFilterSession::create(pSession, pService, this);
}

// static
json_t* BinlogFilter::diagnostics() const
{
    return NULL;
}

// static
uint64_t BinlogFilter::getCapabilities() const
{
    return RCAP_TYPE_STMT_OUTPUT;
}
