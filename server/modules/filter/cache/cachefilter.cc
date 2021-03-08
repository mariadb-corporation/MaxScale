/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachefilter.hh"

#include <maxbase/alloc.h>
#include <maxscale/jansson.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/paths.h>
#include <maxscale/utils.h>

#include "cacheconfig.hh"
#include "cachemt.hh"
#include "cachept.hh"

using std::auto_ptr;
using std::string;

namespace
{

static char VERSION_STRING[] = "V1.0.0";

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
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_FILTER);

    const MXS_FILTER_DEF* pFilterDef = pArgs->argv[0].value.filter;
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
    };

    static bool populated = false;

    if (!populated)
    {
        CacheConfig::specification().populate(info);
        populated = true;
    }

    return &info;
}

//
// CacheFilter
//

CacheFilter::CacheFilter(const std::string& name)
    : m_config(name)
{
}

CacheFilter::~CacheFilter()
{
}

// static
CacheFilter* CacheFilter::create(const char* zName, MXS_CONFIG_PARAMETER* ppParams)
{
    CacheFilter* pFilter = new CacheFilter(zName);

    if (pFilter)
    {
        Cache* pCache = NULL;

        if (CacheConfig::specification().configure(pFilter->m_config, *ppParams))
        {
            switch (pFilter->m_config.thread_model.get())
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
