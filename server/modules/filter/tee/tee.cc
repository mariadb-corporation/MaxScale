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
 * @file tee.cc  A filter that splits the processing pipeline in two
 */

#define MXS_MODULE_NAME "tee"

#include <maxscale/ccdefs.hh>

#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/log.h>
#include <maxscale/modulecmd.hh>

#include "tee.hh"
#include "teesession.hh"

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

Tee::Tee(SERVICE* service,
         std::string user,
         std::string remote,
         pcre2_code* match,
         std::string match_string,
         pcre2_code* exclude,
         std::string exclude_string)
    : m_service(service)
    , m_user(user)
    , m_source(remote)
    , m_match_code(match)
    , m_exclude_code(exclude)
    , m_match(match_string)
    , m_exclude(exclude_string)
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
Tee* Tee::create(const char* name, MXS_CONFIG_PARAMETER* params)
{
    SERVICE* service = config_get_service(params, "service");
    const char* source = config_get_string(params, "source");
    const char* user = config_get_string(params, "user");
    uint32_t cflags = config_get_enum(params, "options", option_values);
    pcre2_code* match = config_get_compiled_regex(params, "match", cflags, NULL);
    pcre2_code* exclude = config_get_compiled_regex(params, "exclude", cflags, NULL);
    const char* match_str = config_get_string(params, "match");
    const char* exclude_str = config_get_string(params, "exclude");

    Tee* my_instance = new(std::nothrow) Tee(service,
                                             source,
                                             user,
                                             match,
                                             match_str,
                                             exclude,
                                             exclude_str);

    if (my_instance == NULL)
    {
        pcre2_code_free(match);
        pcre2_code_free(exclude);
    }

    return my_instance;
}

TeeSession* Tee::newSession(MXS_SESSION* pSession)
{
    return TeeSession::create(this, pSession);
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
 * @param   dcb     The DCB for diagnostic output
 */
void Tee::diagnostics(DCB* dcb)
{
    if (m_source.length())
    {
        dcb_printf(dcb,
                   "\t\tLimit to connections from       %s\n",
                   m_source.c_str());
    }
    dcb_printf(dcb,
               "\t\tDuplicate statements to service		%s\n",
               m_service->name);
    if (m_user.length())
    {
        dcb_printf(dcb,
                   "\t\tLimit to user			%s\n",
                   m_user.c_str());
    }
    if (m_match.length())
    {
        dcb_printf(dcb,
                   "\t\tInclude queries that match		%s\n",
                   m_match.c_str());
    }
    if (m_exclude.c_str())
    {
        dcb_printf(dcb,
                   "\t\tExclude queries that match		%s\n",
                   m_exclude.c_str());
    }
    dcb_printf(dcb, "\t\tFilter enabled: %s\n", m_enabled ? "yes" : "no");
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
json_t* Tee::diagnostics_json() const
{
    json_t* rval = json_object();

    if (m_source.length())
    {
        json_object_set_new(rval, "source", json_string(m_source.c_str()));
    }

    json_object_set_new(rval, "service", json_string(m_service->name));

    if (m_user.length())
    {
        json_object_set_new(rval, "user", json_string(m_user.c_str()));
    }

    if (m_match.length())
    {
        json_object_set_new(rval, "match", json_string(m_match.c_str()));
    }

    if (m_exclude.length())
    {
        json_object_set_new(rval, "exclude", json_string(m_exclude.c_str()));
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
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A tee piece in the filter plumbing",
        "V1.1.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &Tee::s_object,
        NULL,                               /* Process init. */
        NULL,                               /* Process finish. */
        NULL,                               /* Thread init. */
        NULL,                               /* Thread finish. */
        {
            {"service",                      MXS_MODULE_PARAM_SERVICE,NULL, MXS_MODULE_OPT_REQUIRED},
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
