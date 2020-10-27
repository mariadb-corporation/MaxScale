/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
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

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

Tee::Tee(const char* name, mxs::ConfigParameters* params)
    : m_name(name)
    , m_target(params->get_target(params->contains("service") ? "service" : "target"))
    , m_user(params->get_string("user"))
    , m_source(params->get_string("source"))
    , m_match(params->get_string("match"), params->get_enum("options", option_values))
    , m_exclude(params->get_string("exclude"), params->get_enum("options", option_values))
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
Tee* Tee::create(const char* name, mxs::ConfigParameters* params)
{
    Tee* rv = nullptr;

    if (params->contains_all({"service", "target"}))
    {
        MXS_ERROR("Both `service` and `target` cannot be defined at the same time");
    }
    else
    {
        rv = new Tee(name, params);
    }

    return rv;
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

    if (m_source.length())
    {
        json_object_set_new(rval, "source", json_string(m_source.c_str()));
    }

    json_object_set_new(rval, "target", json_string(m_target->name()));

    if (m_user.length())
    {
        json_object_set_new(rval, "user", json_string(m_user.c_str()));
    }

    if (m_match)
    {
        json_object_set_new(rval, "match", json_string(m_match.pattern().c_str()));
    }

    if (m_exclude)
    {
        json_object_set_new(rval, "exclude", json_string(m_exclude.pattern().c_str()));
    }

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
        {
            {"service",                      MXS_MODULE_PARAM_SERVICE, NULL},
            {"target",                       MXS_MODULE_PARAM_TARGET,  NULL},
            {"match",                        MXS_MODULE_PARAM_REGEX},
            {"exclude",                      MXS_MODULE_PARAM_REGEX},
            {"source",                       MXS_MODULE_PARAM_STRING},
            {"user",                         MXS_MODULE_PARAM_STRING},
            {
                "options",
                MXS_MODULE_PARAM_ENUM,
                "ignorecase",
                MXS_MODULE_OPT_NONE,
                option_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

MXS_END_DECLS
