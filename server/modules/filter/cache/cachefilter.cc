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

#define MXS_MODULE_NAME "cache"
#include "cachefilter.hh"

#include <maxbase/alloc.h>
#include <maxscale/jansson.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/paths.hh>

#include "cacheconfig.hh"
#include "cachemt.hh"
#include "cachept.hh"

using std::unique_ptr;
using std::string;

namespace
{

static char VERSION_STRING[] = "V1.0.0";
constexpr uint64_t CAPABILITIES = RCAP_TYPE_TRANSACTION_TRACKING | RCAP_TYPE_REQUEST_TRACKING
    | RCAP_TYPE_OLD_PROTOCOL;

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

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        "A caching filter that is capable of caching and returning cached data.",
        VERSION_STRING,
        CAPABILITIES,
        &mxs::FilterApi<CacheFilter>::s_api,
        cache_process_init,     /* Process init. */
        nullptr,                /* Process finish. */
        nullptr,                /* Thread init. */
        nullptr,                /* Thread finish. */
        CacheConfig::specification()
    };

    return &info;
}

//
// CacheFilter
//

CacheFilter::CacheFilter(const char* zName)
    : m_config(zName, this)
{
}

CacheFilter::~CacheFilter()
{
}

// static
CacheFilter* CacheFilter::create(const char* zName)
{
    return new CacheFilter(zName);
}

bool CacheFilter::post_configure()
{
    Cache* pCache = nullptr;

    switch (m_config.thread_model)
    {
    case CACHE_THREAD_MODEL_MT:
        MXS_NOTICE("Creating shared cache.");
        MXS_EXCEPTION_GUARD(pCache = CacheMT::create(m_config.name(), &m_config));
        break;

    case CACHE_THREAD_MODEL_ST:
        MXS_NOTICE("Creating thread specific cache.");
        MXS_EXCEPTION_GUARD(pCache = CachePT::create(m_config.name(), &m_config));
        break;

    default:
        mxb_assert(!true);
    }

    if (pCache)
    {
        Storage::Limits limits;
        pCache->get_limits(&limits);

        uint32_t max_resultset_size = m_config.max_resultset_size;

        if (max_resultset_size == 0)
        {
            max_resultset_size = std::numeric_limits<uint32_t>::max();
        }

        if (max_resultset_size > limits.max_value_size)
        {
            MXS_WARNING("The used cache storage limits the maximum size of a value to "
                        "%u bytes, but either no value has been specified for "
                        "max_resultset_size or the value is larger. Setting "
                        "max_resultset_size to the maximum size.", limits.max_value_size);
            m_config.max_resultset_size = limits.max_value_size;
        }

        m_sCache.reset(pCache);
    }

    return pCache != nullptr;
}

CacheFilterSession* CacheFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    CacheFilterSession* pFilter_session = nullptr;

    auto sSession_cache = SessionCache::create(m_sCache.get());

    if (sSession_cache)
    {
        pFilter_session = CacheFilterSession::create(std::move(sSession_cache), pSession, pService);
    }

    return pFilter_session;
}

// static
json_t* CacheFilter::diagnostics() const
{
    return m_sCache->show_json();
}

uint64_t CacheFilter::getCapabilities() const
{
    return CAPABILITIES;
}
