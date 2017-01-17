/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachefilter.hh"
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
#include <maxscale/modulecmd.h>
#include "cachemt.hh"
#include "cachept.hh"

using std::auto_ptr;
using std::string;

namespace
{

static char VERSION_STRING[] = "V1.0.0";

/**
 * Frees all data of a config object, but not the object itself
 *
 * @param pConfig  Pointer to a config object.
 */
void cache_config_finish(CACHE_CONFIG& config)
{
    MXS_FREE(config.rules);
    MXS_FREE(config.storage);
    MXS_FREE(config.storage_options);
    MXS_FREE(config.storage_argv); // The items need not be freed, they point into storage_options.

    config.max_resultset_rows = 0;
    config.max_resultset_size = 0;
    config.rules = NULL;
    config.storage = NULL;
    config.storage_options = NULL;
    config.storage_argc = 0;
    config.storage_argv = NULL;
    config.hard_ttl = 0;
    config.soft_ttl = 0;
    config.debug = 0;
}

/**
 * Frees all data of a config object, and the object itself
 *
 * @param pConfig  Pointer to a config object.
 */
void cache_config_free(CACHE_CONFIG* pConfig)
{
    if (pConfig)
    {
        cache_config_finish(*pConfig);
        MXS_FREE(pConfig);
    }
}

/**
 * Resets the data without freeing anything.
 *
 * @param config  Reference to a config object.
 */
void cache_config_reset(CACHE_CONFIG& config)
{
    memset(&config, 0, sizeof(config));
}

/**
 * Implement "call command cache show ..."
 *
 * @param pArgs  The arguments of the command.
 *
 * @return True, if the command was handled.
 */
bool cache_command_show(const MODULECMD_ARG* pArgs)
{
    ss_dassert(pArgs->argc == 2);
    ss_dassert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_OUTPUT);
    ss_dassert(MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_FILTER);

    DCB* pDcb = pArgs->argv[0].value.dcb;
    ss_dassert(pDcb);

    const MXS_FILTER_DEF* pFilterDef = pArgs->argv[1].value.filter;
    ss_dassert(pFilterDef);
    CacheFilter* pFilter = reinterpret_cast<CacheFilter*>(filter_def_get_instance(pFilterDef));

    MXS_EXCEPTION_GUARD(pFilter->cache().show(pDcb));

    return true;
}

}

//
// Global symbols of the Module
//

// Enumeration values for `cached_data`
static const MXS_ENUM_VALUE cached_data_values[] =
{
    {"shared",          CACHE_THREAD_MODEL_MT},
    {"thread_specific", CACHE_THREAD_MODEL_ST},
    {NULL}
};

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t show_argv[] =
    {
        { MODULECMD_ARG_OUTPUT, "The output dcb" },
        { MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Cache name" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "show", cache_command_show,
                               MXS_ARRAY_NELEMS(show_argv), show_argv);

    MXS_NOTICE("Initialized cache module %s.\n", VERSION_STRING);

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A caching filter that is capable of caching and returning cached data.",
        VERSION_STRING,
        &CacheFilter::s_object,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                "storage",
                MXS_MODULE_PARAM_STRING,
                NULL,
                MXS_MODULE_OPT_REQUIRED
            },
            {
                "storage_options",
                MXS_MODULE_PARAM_STRING
            },
            {
                "hard_ttl",
                MXS_MODULE_PARAM_COUNT,
                CACHE_DEFAULT_MAX_RESULTSET_SIZE
            },
            {
                "soft_ttl",
                MXS_MODULE_PARAM_COUNT,
                CACHE_DEFAULT_SOFT_TTL
            },
            {
                "max_resultset_rows",
                MXS_MODULE_PARAM_COUNT,
                CACHE_DEFAULT_MAX_RESULTSET_ROWS
            },
            {
                "max_resultset_size",
                MXS_MODULE_PARAM_COUNT,
                CACHE_DEFAULT_MAX_RESULTSET_SIZE
            },
            {
                "max_count",
                MXS_MODULE_PARAM_COUNT,
                CACHE_DEFAULT_MAX_COUNT
            },
            {
                "max_size",
                MXS_MODULE_PARAM_COUNT,
                CACHE_DEFAULT_THREAD_MODEL
            },
            {
                "rules",
                MXS_MODULE_PARAM_PATH
            },
            {
                "debug",
                MXS_MODULE_PARAM_COUNT,
                CACHE_DEFAULT_DEBUG
            },
            {
                "cached_data",
                MXS_MODULE_PARAM_ENUM,
                CACHE_DEFAULT_THREAD_MODEL,
                MXS_MODULE_OPT_NONE,
                cached_data_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
};

//
// CacheFilter
//

CacheFilter::CacheFilter()
{
    cache_config_reset(m_config);
}

CacheFilter::~CacheFilter()
{
    cache_config_finish(m_config);
}

// static
CacheFilter* CacheFilter::create(const char* zName, char** pzOptions, CONFIG_PARAMETER* ppParams)
{
    CacheFilter* pFilter = new CacheFilter;

    if (pFilter)
    {
        Cache* pCache = NULL;

        if (process_params(pzOptions, ppParams, pFilter->m_config))
        {
            switch (pFilter->m_config.thread_model)
            {
            case CACHE_THREAD_MODEL_MT:
                MXS_NOTICE("Creating shared cache.");
                MXS_EXCEPTION_GUARD(pCache = CacheMT::Create(zName, &pFilter->m_config));
                break;

            case CACHE_THREAD_MODEL_ST:
                MXS_NOTICE("Creating thread specific cache.");
                MXS_EXCEPTION_GUARD(pCache = CachePT::Create(zName, &pFilter->m_config));
                break;

            default:
                ss_dassert(!true);
            }
        }

        if (pCache)
        {
            pFilter->m_sCache = auto_ptr<Cache>(pCache);
        }
        else
        {
            cache_config_finish(pFilter->m_config);
            delete pFilter;
            pFilter = NULL;
        }
    }

    return pFilter;
}

CacheFilterSession* CacheFilter::newSession(SESSION* pSession)
{
    return CacheFilterSession::Create(m_sCache.get(), pSession);
}

// static
void CacheFilter::diagnostics(DCB* pDcb)
{
    m_sCache->show(pDcb);
}

// static
uint64_t CacheFilter::getCapabilities()
{
    return RCAP_TYPE_TRANSACTION_TRACKING;
}

// static
bool CacheFilter::process_params(char **pzOptions, CONFIG_PARAMETER *ppParams, CACHE_CONFIG& config)
{
    bool error = false;

    config.debug = config_get_integer(ppParams, "debug");
    config.hard_ttl = config_get_integer(ppParams, "hard_ttl");
    config.soft_ttl = config_get_integer(ppParams, "soft_ttl");
    config.max_size = config_get_integer(ppParams, "max_size");
    config.max_count = config_get_integer(ppParams, "max_count");
    config.storage = MXS_STRDUP(config_get_string(ppParams, "storage"));
    config.max_resultset_rows = config_get_integer(ppParams, "max_resultset_rows");
    config.max_resultset_size = config_get_integer(ppParams, "max_resultset_size");
    config.thread_model = static_cast<cache_thread_model_t>(config_get_enum(ppParams,
                                                                            "cached_data",
                                                                            cached_data_values));

    if (!config.storage)
    {
        error = true;
    }

    if ((config.debug < CACHE_DEBUG_MIN) || (config.debug > CACHE_DEBUG_MAX))
    {
        MXS_ERROR("The value of the configuration entry 'debug' must "
                  "be between %d and %d, inclusive.",
                  CACHE_DEBUG_MIN, CACHE_DEBUG_MAX);
        error = true;
    }

    config.rules = config_copy_string(ppParams, "rules");

    const CONFIG_PARAMETER *pParam = config_get_param(ppParams, "storage_options");

    if (pParam)
    {
        config.storage_options = MXS_STRDUP(pParam->value);

        if (config.storage_options)
        {
            int argc = 1;
            char *arg = config.storage_options;

            while ((arg = strchr(arg, ',')))
            {
                arg = arg + 1;
                ++argc;
            }

            config.storage_argv = (char**) MXS_MALLOC((argc + 1) * sizeof(char*));

            if (config.storage_argv)
            {
                config.storage_argc = argc;

                int i = 0;
                arg = config.storage_options;
                config.storage_argv[i++] = arg;

                while ((arg = strchr(config.storage_options, ',')))
                {
                    *arg = 0;
                    ++arg;
                    config.storage_argv[i++] = arg;
                }

                config.storage_argv[i] = NULL;
            }
            else
            {
                MXS_FREE(config.storage_options);
                config.storage_options = NULL;
            }
        }
        else
        {
            error = true;
        }
    }

    if (!error)
    {
        if (config.soft_ttl > config.hard_ttl)
        {
            MXS_WARNING("The value of 'soft_ttl' must be less than or equal to that of 'hard_ttl'. "
                        "Setting 'soft_ttl' to the same value as 'hard_ttl'.");
            config.soft_ttl = config.hard_ttl;
        }

        if (config.max_size < config.max_resultset_size)
        {
            MXS_ERROR("The value of 'max_size' must be at least as larged as that "
                      "of 'max_resultset_size'.");

            error = true;
        }
    }

    if (error)
    {
        cache_config_finish(config);
    }

    return !error;
}
