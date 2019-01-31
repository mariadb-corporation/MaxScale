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

#define MXS_MODULE_NAME "cache"
#include "cachefilter.hh"

#include <maxscale/alloc.h>
#include <maxscale/jansson.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/paths.h>
#include <maxscale/utils.h>

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
    MXS_FREE(config.storage_argv);      // The items need not be freed, they point into storage_options.

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
    config.thread_model = CACHE_DEFAULT_THREAD_MODEL;
    config.selects = CACHE_DEFAULT_SELECTS;
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

    config.thread_model = CACHE_DEFAULT_THREAD_MODEL;
    config.selects = CACHE_DEFAULT_SELECTS;
}

/**
 * Implement "call command cache show ..."
 *
 * @param pArgs  The arguments of the command.
 *
 * @return True, if the command was handled.
 */
bool cache_command_show(const MODULECMD_ARG* pArgs, json_t** output)
{
    mxb_assert(pArgs->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_FILTER);

    const MXS_FILTER_DEF* pFilterDef = pArgs->argv[1].value.filter;
    mxb_assert(pFilterDef);
    CacheFilter* pFilter = reinterpret_cast<CacheFilter*>(filter_def_get_instance(pFilterDef));

    MXS_EXCEPTION_GUARD(*output = pFilter->cache().show_json());

    return true;
}

int cache_process_init()
{
    uint32_t jit_available;
    pcre2_config(PCRE2_CONFIG_JIT, &jit_available);

    if (!jit_available)
    {
        MXS_WARNING("pcre2 JIT is not available; regex matching will not be "
                    "as efficient as it could be.");
    }

    return 0;
}
}

//
// Global symbols of the Module
//

// Enumeration values for `cached_data`
static const MXS_ENUM_VALUE parameter_cached_data_values[] =
{
    {"shared",          CACHE_THREAD_MODEL_MT},
    {"thread_specific", CACHE_THREAD_MODEL_ST},
    {NULL}
};

// Enumeration values for `selects`
static const MXS_ENUM_VALUE parameter_selects_values[] =
{
    {"assume_cacheable", CACHE_SELECTS_ASSUME_CACHEABLE},
    {"verify_cacheable", CACHE_SELECTS_VERIFY_CACHEABLE},
    {NULL}
};

// Enumeration values for `cache_in_transaction`
static const MXS_ENUM_VALUE parameter_cache_in_trxs_values[] =
{
    {"never",                  CACHE_IN_TRXS_NEVER    },
    {"read_only_transactions", CACHE_IN_TRXS_READ_ONLY},
    {"all_transactions",       CACHE_IN_TRXS_ALL      },
    {NULL}
};

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t show_argv[] =
    {
        {MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Cache name"}
    };

    modulecmd_register_command(MXS_MODULE_NAME,
                               "show",
                               MODULECMD_TYPE_PASSIVE,
                               cache_command_show,
                               MXS_ARRAY_NELEMS(show_argv),
                               show_argv,
                               "Show cache filter statistics");

    MXS_NOTICE("Initialized cache module %s.\n", VERSION_STRING);

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A caching filter that is capable of caching and returning cached data.",
        VERSION_STRING,
        RCAP_TYPE_TRANSACTION_TRACKING,
        &CacheFilter::s_object,
        cache_process_init, /* Process init. */
        NULL,               /* Process finish. */
        NULL,               /* Thread init. */
        NULL,               /* Thread finish. */
        {
            {
                "storage",
                MXS_MODULE_PARAM_STRING,
                CACHE_ZDEFAULT_STORAGE
            },
            {
                "storage_options",
                MXS_MODULE_PARAM_STRING
            },
            {
                "hard_ttl",
                MXS_MODULE_PARAM_COUNT,
                CACHE_ZDEFAULT_HARD_TTL
            },
            {
                "soft_ttl",
                MXS_MODULE_PARAM_COUNT,
                CACHE_ZDEFAULT_SOFT_TTL
            },
            {
                "max_resultset_rows",
                MXS_MODULE_PARAM_COUNT,
                CACHE_ZDEFAULT_MAX_RESULTSET_ROWS
            },
            {
                "max_resultset_size",
                MXS_MODULE_PARAM_SIZE,
                CACHE_ZDEFAULT_MAX_RESULTSET_SIZE
            },
            {
                "max_count",
                MXS_MODULE_PARAM_COUNT,
                CACHE_ZDEFAULT_MAX_COUNT
            },
            {
                "max_size",
                MXS_MODULE_PARAM_SIZE,
                CACHE_ZDEFAULT_MAX_SIZE
            },
            {
                "rules",
                MXS_MODULE_PARAM_PATH
            },
            {
                "debug",
                MXS_MODULE_PARAM_COUNT,
                CACHE_ZDEFAULT_DEBUG
            },
            {
                "cached_data",
                MXS_MODULE_PARAM_ENUM,
                CACHE_ZDEFAULT_THREAD_MODEL,
                MXS_MODULE_OPT_NONE,
                parameter_cached_data_values
            },
            {
                "selects",
                MXS_MODULE_PARAM_ENUM,
                CACHE_ZDEFAULT_SELECTS,
                MXS_MODULE_OPT_NONE,
                parameter_selects_values
            },
            {
                "cache_in_transactions",
                MXS_MODULE_PARAM_ENUM,
                CACHE_ZDEFAULT_CACHE_IN_TRXS,
                MXS_MODULE_OPT_NONE,
                parameter_cache_in_trxs_values
            },
            {
                "enabled",
                MXS_MODULE_PARAM_BOOL,
                CACHE_ZDEFAULT_ENABLED
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

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
CacheFilter* CacheFilter::create(const char* zName, MXS_CONFIG_PARAMETER* ppParams)
{
    CacheFilter* pFilter = new CacheFilter;

    if (pFilter)
    {
        Cache* pCache = NULL;

        if (process_params(ppParams, pFilter->m_config))
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
                mxb_assert(!true);
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

CacheFilterSession* CacheFilter::newSession(MXS_SESSION* pSession)
{
    return CacheFilterSession::Create(m_sCache.get(), pSession);
}

// static
void CacheFilter::diagnostics(DCB* pDcb)
{
    m_sCache->show(pDcb);
}

// static
json_t* CacheFilter::diagnostics_json() const
{
    return m_sCache->show_json();
}

uint64_t CacheFilter::getCapabilities()
{
    return RCAP_TYPE_NONE;
}

// static
bool CacheFilter::process_params(MXS_CONFIG_PARAMETER* ppParams, CACHE_CONFIG& config)
{
    bool error = false;

    config.debug = ppParams->get_integer("debug");
    config.hard_ttl = ppParams->get_integer("hard_ttl");
    config.soft_ttl = ppParams->get_integer("soft_ttl");
    config.max_size = config_get_size(ppParams, "max_size");
    config.max_count = ppParams->get_integer("max_count");
    config.storage = MXS_STRDUP(config_get_string(ppParams, "storage"));
    config.max_resultset_rows = ppParams->get_integer("max_resultset_rows");
    config.max_resultset_size = config_get_size(ppParams, "max_resultset_size");
    config.thread_model = static_cast<cache_thread_model_t>(ppParams->get_enum("cached_data",
                                                                               parameter_cached_data_values));
    config.selects = static_cast<cache_selects_t>(ppParams->get_enum("selects",
                                                                     parameter_selects_values));
    config.cache_in_trxs = static_cast<cache_in_trxs_t>(ppParams->get_enum("cache_in_transactions",
                                                                           parameter_cache_in_trxs_values));
    config.enabled = config_get_bool(ppParams, "enabled");

    if (!config.storage)
    {
        error = true;
    }

    if ((config.debug < CACHE_DEBUG_MIN) || (config.debug > CACHE_DEBUG_MAX))
    {
        MXS_ERROR("The value of the configuration entry 'debug' must "
                  "be between %d and %d, inclusive.",
                  CACHE_DEBUG_MIN,
                  CACHE_DEBUG_MAX);
        error = true;
    }

    config.rules = config_copy_string(ppParams, "rules");

    const MXS_CONFIG_PARAMETER* pParam = config_get_param(ppParams, "storage_options");

    if (pParam)
    {
        config.storage_options = MXS_STRDUP(pParam->value);

        if (config.storage_options)
        {
            int argc = 1;
            char* arg = config.storage_options;

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

        if (config.max_resultset_size == 0)
        {
            if (config.max_size != 0)
            {
                // If a specific size has been configured for 'max_size' but 'max_resultset_size'
                // has not been specifically set, then we silently set it to the same as 'max_size'.
                config.max_resultset_size = config.max_size;
            }
        }
        else
        {
            mxb_assert(config.max_resultset_size != 0);

            if ((config.max_size != 0) && (config.max_resultset_size > config.max_size))
            {
                MXS_WARNING("The value of 'max_resultset_size' %ld should not be larger than "
                            "the value of 'max_size' %ld. Adjusting the value of 'max_resultset_size' "
                            "down to %ld.",
                            config.max_resultset_size,
                            config.max_size,
                            config.max_size);
                config.max_resultset_size = config.max_size;
            }
        }
    }

    if (error)
    {
        cache_config_finish(config);
    }

    return !error;
}
