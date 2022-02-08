/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * @file tee.cc  A filter that splits the processing pipeline in two
 */

#define MXS_MODULE_NAME "tee"

#include <maxscale/ccdefs.hh>

#include <maxbase/alloc.h>
#include <maxscale/modinfo.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/pcre2.hh>

#include "tee.hh"
#include "teesession.hh"

namespace
{
namespace cfg = mxs::config;

class TeeSpecification : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:
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

TeeSpecification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamTarget s_target(
    &s_spec, "target", "The target where the queries are duplicated",
    cfg::Param::OPTIONAL, cfg::Param::AT_RUNTIME);

cfg::ParamService s_service(
    &s_spec, "service", "The service where the queries are duplicated",
    cfg::Param::OPTIONAL, cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_match(
    &s_spec, "match", "Only include queries matching this pattern",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_exclude(
    &s_spec, "exclude", "Exclude queries matching this pattern",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamString s_source(
    &s_spec, "source", "Only include queries done from this address",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamString s_user(
    &s_spec, "user", "Only include queries done by this user",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamEnum<uint32_t> s_options(
    &s_spec, "options", "Regular expression options",
    {
        {PCRE2_CASELESS, "ignorecase"},
        {0, "case"},
        {PCRE2_EXTENDED, "extended"},
    }, 0, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_sync(
    &s_spec, "sync", "Wait for both results before routing more queries",
    false, cfg::Param::AT_RUNTIME);

template<class Params>
bool TeeSpecification::do_post_validate(Params params) const
{
    bool ok = false;

    if (!s_target.get(params) && !s_service.get(params))
    {
        // The `service` parameter is deprecated, don't mention it in the hopes that people stop using it.
        MXS_ERROR("Parameter `target` must be defined");
    }
    else if (s_target.get(params) && s_service.get(params))
    {
        MXS_ERROR("Both `service` and `target` cannot be defined at the same time");
    }
    else
    {
        ok = true;
    }

    return ok;
}
}

Tee::Config::Config(const char* name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&Config::m_v, &Values::target, &s_target);
    add_native(&Config::m_v, &Values::service, &s_service);
    add_native(&Config::m_v, &Values::user, &s_user);
    add_native(&Config::m_v, &Values::source, &s_source);
    add_native(&Config::m_v, &Values::match, &s_match);
    add_native(&Config::m_v, &Values::exclude, &s_exclude);
    add_native(&Config::m_v, &Values::sync, &s_sync);
}

bool Tee::Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    if (m_v.service)
    {
        mxb_assert(!m_v.target);
        m_v.target = m_v.service;
    }

    m_values.assign(m_v);
    return true;
}

Tee::Tee(const char* name)
    : m_name(name)
    , m_config(name)
    , m_enabled(true)
{
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file).
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
Tee* Tee::create(const char* name)
{
    return new Tee(name);
}

TeeSession* Tee::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return TeeSession::create(this, pSession, pService);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 */
json_t* Tee::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "enabled", json_boolean(m_enabled));

    return rval;
}

static bool enable_tee(const MODULECMD_ARG* argv, json_t** output)
{
    Tee* instance = reinterpret_cast<Tee*>(filter_def_get_instance(argv->argv[0].value.filter));
    instance->set_enabled(true);
    return true;
}

static bool disable_tee(const MODULECMD_ARG* argv, json_t** output)
{
    Tee* instance = reinterpret_cast<Tee*>(filter_def_get_instance(argv->argv[0].value.filter));
    instance->set_enabled(false);
    return true;
}

MXS_BEGIN_DECLS

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    modulecmd_arg_type_t argv[] =
    {
        {
            MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "Filter to modify"
        }
    };

    modulecmd_register_command(MXS_MODULE_NAME,
                               "enable",
                               MODULECMD_TYPE_ACTIVE,
                               enable_tee,
                               1,
                               argv,
                               "Enable a tee filter instance");
    modulecmd_register_command(MXS_MODULE_NAME,
                               "disable",
                               MODULECMD_TYPE_ACTIVE,
                               disable_tee,
                               1,
                               argv,
                               "Disable a tee filter instance");

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        "A tee piece in the filter plumbing",
        "V1.1.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::FilterApi<Tee>::s_api,
        NULL,                               /* Process init. */
        NULL,                               /* Process finish. */
        NULL,                               /* Thread init. */
        NULL,                               /* Thread finish. */
        &s_spec
    };

    return &info;
}

MXS_END_DECLS
