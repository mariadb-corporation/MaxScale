/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file tee.cc  A filter that splits the processing pipeline in two
 */

#define MXS_MODULE_NAME "tee"

#include <maxscale/cppdefs.hh>

#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/log_manager.h>

#include "tee.hh"
#include "local_client.hh"
#include "teesession.hh"

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", REG_ICASE},
    {"case", 0},
    {"extended", REG_EXTENDED},
    {NULL}
};

Tee::Tee(SERVICE* service, const char* user, const char* remote,
         const char* match, const char* nomatch, int cflags):
    m_service(service),
    m_user(user),
    m_source(remote),
    m_match(match),
    m_nomatch(nomatch)
{
    if (*match)
    {
        ss_debug(int rc = )regcomp(&m_re, match, cflags);
        ss_dassert(rc == 0);
    }

    if (*nomatch)
    {
        ss_debug(int rc = )regcomp(&m_nore, nomatch, cflags);
        ss_dassert(rc == 0);
    }
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
Tee* Tee::create(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    Tee *my_instance = NULL;

    SERVICE* service = config_get_service(params, "service");
    const char* source = config_get_string(params, "source");
    const char* user = config_get_string(params, "user");
    const char* match = config_get_string(params, "match");
    const char* nomatch = config_get_string(params, "exclude");

    int cflags = config_get_enum(params, "options", option_values);
    regex_t re;
    regex_t nore;

    if (*match && regcomp(&re, match, cflags) != 0)
    {
        MXS_ERROR("Invalid regular expression '%s' for the match parameter.", match);
    }
    else if (*nomatch && regcomp(&nore, nomatch, cflags) != 0)
    {
        MXS_ERROR("Invalid regular expression '%s' for the nomatch parameter.", nomatch);

        if (*match)
        {
            regfree(&re);
        }
    }
    else
    {
        my_instance = new (std::nothrow) Tee(service, source, user, match, nomatch, cflags);
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
void Tee::diagnostics(DCB *dcb)
{
    if (m_source.length())
    {
        dcb_printf(dcb, "\t\tLimit to connections from 		%s\n",
                   m_source.c_str());
    }
    dcb_printf(dcb, "\t\tDuplicate statements to service		%s\n",
               m_service->name);
    if (m_user.length())
    {
        dcb_printf(dcb, "\t\tLimit to user			%s\n",
                   m_user.c_str());
    }
    if (m_match.length())
    {
        dcb_printf(dcb, "\t\tInclude queries that match		%s\n",
                   m_match.c_str());
    }
    if (m_nomatch.c_str())
    {
        dcb_printf(dcb, "\t\tExclude queries that match		%s\n",
                   m_nomatch.c_str());
    }
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

    if (m_nomatch.length())
    {
        json_object_set_new(rval, "exclude", json_string(m_nomatch.c_str()));
    }

    return rval;
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

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A tee piece in the filter plumbing",
        "V1.1.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &Tee::s_object,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"service", MXS_MODULE_PARAM_SERVICE, NULL, MXS_MODULE_OPT_REQUIRED},
            {"match", MXS_MODULE_PARAM_STRING},
            {"exclude", MXS_MODULE_PARAM_STRING},
            {"source", MXS_MODULE_PARAM_STRING},
            {"user", MXS_MODULE_PARAM_STRING},
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
