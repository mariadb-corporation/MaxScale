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

static const CACHE_CONFIG DEFAULT_CONFIG =
{
    CACHE_DEFAULT_MAX_RESULTSET_ROWS,
    CACHE_DEFAULT_MAX_RESULTSET_SIZE,
    NULL,                              // rules
    NULL,                              // storage
    NULL,                              // storage_options
    NULL,                              // storage_argv
    0,                                 // storage_argc
    CACHE_DEFAULT_HARD_TTL,
    CACHE_DEFAULT_SOFT_TTL,
    CACHE_DEFAULT_MAX_COUNT,
    CACHE_DEFAULT_MAX_SIZE,
    CACHE_DEFAULT_DEBUG,
    CACHE_DEFAULT_THREAD_MODEL,
};

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

    const FILTER_DEF* pFilterDef = pArgs->argv[1].value.filter;
    ss_dassert(pFilterDef);

    if (strcmp(pFilterDef->module, "cache") == 0)
    {
        CacheFilter* pFilter = reinterpret_cast<CacheFilter*>(pFilterDef->filter);

        pFilter->cache().show(pDcb);
    }
    else
    {
        dcb_printf(pDcb, "Filter %s exists, but it is not a cache.", pFilterDef->name);
    }

    return true;
}

/**
 * Get a 32-bit unsigned value.
 *
 * Note that the value itself is converted a signed integer to detect
 * configuration errors.
 *
 * @param param   The parameter entry.
 * @param pValue  Pointer to variable where result is stored.
 *
 * @return True if the parameter was an unsigned integer.
 */
bool config_get_uint32(const FILTER_PARAMETER& param, uint32_t* pValue)
{
    bool rv = false;
    char* end;
    int32_t value = strtol(param.value, &end, 0);

    if ((*end == 0) && (value >= 0))
    {
        *pValue = value;
        rv = true;
    }
    else
    {
        MXS_ERROR("The value of the configuration entry '%s' must "
                  "be an integer larger than or equal to 0.", param.name);
    }

    return rv;
}

/**
 * Get a 64-bit unsigned value.
 *
 * Note that the value itself is converted a signed integer to detect
 * configuration errors.
 *
 * @param param   The parameter entry.
 * @param pValue  Pointer to variable where result is stored.
 *
 * @return True if the parameter was an unsigned integer.
 */
bool config_get_uint64(const FILTER_PARAMETER& param, uint64_t* pValue)
{
    bool rv = false;
    char* end;
    int64_t value = strtoll(param.value, &end, 0);

    if ((*end == 0) && (value >= 0))
    {
        *pValue = value;
        rv = true;
    }
    else
    {
        MXS_ERROR("The value of the configuration entry '%s' must "
                  "be an integer larger than or equal to 0.", param.name);
    }

    return rv;
}

}

//
// Global symbols of the Module
//

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t show_argv[] =
    {
        { MODULECMD_ARG_OUTPUT, "The output dcb" },
        { MODULECMD_ARG_FILTER, "Cache name" }
    };
    
    modulecmd_register_command("cache", "show", cache_command_show,
                               MXS_ARRAY_NELEMS(show_argv), show_argv);
    
    MXS_NOTICE("Initialized cache module %s.\n", VERSION_STRING);

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        FILTER_VERSION,
        "A caching filter that is capable of caching and returning cached data.",
        VERSION_STRING,
        &CacheFilter::s_object
    };

    return &info;
};

//
// CacheFilter
//

CacheFilter::CacheFilter()
    : m_config(DEFAULT_CONFIG)
{
}

CacheFilter::~CacheFilter()
{
    cache_config_finish(m_config);
}

// static
CacheFilter* CacheFilter::create(const char* zName, char** pzOptions, FILTER_PARAMETER** ppParams)
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
bool CacheFilter::process_params(char **pzOptions, FILTER_PARAMETER **ppParams, CACHE_CONFIG& config)
{
    bool error = false;

    for (int i = 0; ppParams[i]; ++i)
    {
        const FILTER_PARAMETER *pParam = ppParams[i];

        if (strcmp(pParam->name, "max_resultset_rows") == 0)
        {
            if (!config_get_uint64(*pParam, &config.max_resultset_rows))
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "max_resultset_size") == 0)
        {
            if (config_get_uint64(*pParam, &config.max_resultset_size))
            {
                config.max_resultset_size *= 1024;
            }
            else
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "rules") == 0)
        {
            if (*pParam->value == '/')
            {
                config.rules = MXS_STRDUP(pParam->value);
            }
            else
            {
                const char* datadir = get_datadir();
                size_t len = strlen(datadir) + 1 + strlen(pParam->value) + 1;

                char *rules = (char*)MXS_MALLOC(len);

                if (rules)
                {
                    sprintf(rules, "%s/%s", datadir, pParam->value);
                    config.rules = rules;
                }
            }

            if (!config.rules)
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "storage_options") == 0)
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
        else if (strcmp(pParam->name, "storage") == 0)
        {
            config.storage = MXS_STRDUP(pParam->value);

            if (!config.storage)
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "hard_ttl") == 0)
        {
            if (!config_get_uint32(*pParam, &config.hard_ttl))
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "soft_ttl") == 0)
        {
            if (!config_get_uint32(*pParam, &config.soft_ttl))
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "max_count") == 0)
        {
            if (!config_get_uint64(*pParam, &config.max_count))
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "max_size") == 0)
        {
            if (config_get_uint64(*pParam, &config.max_size))
            {
                config.max_size = config.max_size * 1024;
            }
            else
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "debug") == 0)
        {
            int v = atoi(pParam->value);

            if ((v >= CACHE_DEBUG_MIN) && (v <= CACHE_DEBUG_MAX))
            {
                config.debug = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be between %d and %d, inclusive.",
                          pParam->name, CACHE_DEBUG_MIN, CACHE_DEBUG_MAX);
                error = true;
            }
        }
        else if (strcmp(pParam->name, "cached_data") == 0)
        {
            if (strcmp(pParam->value, "shared") == 0)
            {
                config.thread_model = CACHE_THREAD_MODEL_MT;
            }
            else if (strcmp(pParam->value, "thread_specific") == 0)
            {
                config.thread_model = CACHE_THREAD_MODEL_ST;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be either 'shared' or 'thread_specific'.", pParam->name);
                error = true;
            }
        }
        else if (!filter_standard_parameter(pParam->name))
        {
            MXS_ERROR("Unknown configuration entry '%s'.", pParam->name);
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
