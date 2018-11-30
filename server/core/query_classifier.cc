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

#include "internal/query_classifier.hh"
#include <inttypes.h>
#include <algorithm>
#include <atomic>
#include <random>
#include <unordered_map>
#include <maxscale/alloc.h>
#include <maxbase/atomic.h>
#include <maxbase/format.hh>
#include <maxscale/config.h>
#include <maxscale/json_api.h>
#include <maxscale/log.h>
#include <maxscale/modutil.hh>
#include <maxscale/pcre2.h>
#include <maxscale/utils.h>
#include <maxscale/jansson.hh>
#include <maxscale/buffer.hh>

#include "internal/config_runtime.h"
#include "internal/modules.h"
#include "internal/trxboundaryparser.hh"

// #define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined (QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

namespace
{

struct type_name_info
{
    const char* name;
    size_t      name_len;
};

const char DEFAULT_QC_NAME[] = "qc_sqlite";
const char QC_TRX_PARSE_USING[] = "QC_TRX_PARSE_USING";

class ThisUnit
{
public:
    ThisUnit()
        : classifier(nullptr)
        , qc_trx_parse_using(QC_TRX_PARSE_USING_PARSER)
        , qc_sql_mode(QC_SQL_MODE_DEFAULT)
        , m_cache_max_size(std::numeric_limits<int64_t>::max())
    {
    }

    ThisUnit(const ThisUnit&) = delete;
    ThisUnit& operator=(const ThisUnit&) = delete;

    QUERY_CLASSIFIER*    classifier;
    qc_trx_parse_using_t qc_trx_parse_using;
    qc_sql_mode_t        qc_sql_mode;

    int64_t cache_max_size() const
    {
        // In principle, std::memory_order_acquire should be used here, but that causes
        // a performance penalty of ~5% when running a sysbench test.
        return m_cache_max_size.load(std::memory_order_relaxed);
    }

    void set_cache_max_size(int64_t cache_max_size)
    {
        // In principle, std::memory_order_release should be used here.
        m_cache_max_size.store(cache_max_size, std::memory_order_relaxed);
    }

private:
    std::atomic<int64_t> m_cache_max_size;
};

static ThisUnit this_unit;

class QCInfoCache;

static thread_local struct
{
    QCInfoCache* pInfo_cache;
} this_thread =
{
    nullptr
};


/**
 * @class QCInfoCache
 *
 * An instance of this class maintains a mapping from a canonical statement to
 * the QC_STMT_INFO object created by the actual query classifier.
 */
class QCInfoCache
{
public:
    QCInfoCache(const QCInfoCache&) = delete;
    QCInfoCache& operator=(const QCInfoCache&) = delete;

    QCInfoCache()
        : m_reng(m_rdev())
    {
        memset(&m_stats, 0, sizeof(m_stats));
    }

    ~QCInfoCache()
    {
        mxb_assert(this_unit.classifier);

        for (auto a : m_infos)
        {
            this_unit.classifier->qc_info_close(a.second.pInfo);
        }
    }

    QC_STMT_INFO* peek(const std::string& canonical_stmt) const
    {
        auto i = m_infos.find(canonical_stmt);

        return i != m_infos.end() ? i->second.pInfo : nullptr;
    }

    QC_STMT_INFO* get(const std::string& canonical_stmt)
    {
        QC_STMT_INFO* pInfo = nullptr;

        auto i = m_infos.find(canonical_stmt);

        if (i != m_infos.end())
        {
            const Entry& entry = i->second;

            if (entry.sql_mode == this_unit.qc_sql_mode)
            {
                mxb_assert(this_unit.classifier);
                this_unit.classifier->qc_info_dup(entry.pInfo);
                pInfo = entry.pInfo;

                ++m_stats.hits;
            }
            else
            {
                // If the sql_mode has changed, we discard the existing result.
                erase(i);

                ++m_stats.misses;
            }
        }
        else
        {
            ++m_stats.misses;
        }

        return pInfo;
    }

    void insert(const std::string& canonical_stmt, QC_STMT_INFO* pInfo)
    {
        mxb_assert(peek(canonical_stmt) == nullptr);
        mxb_assert(this_unit.classifier);

        int64_t cache_max_size = this_unit.cache_max_size() / config_get_global_options()->n_threads;
        int64_t size = canonical_stmt.size();

        if (size <= cache_max_size)
        {
            int64_t required_space = (m_stats.size + size) - cache_max_size;

            if (required_space > 0)
            {
                make_space(required_space);
            }

            if (m_stats.size + size <= cache_max_size)
            {
                this_unit.classifier->qc_info_dup(pInfo);

                m_infos.emplace(canonical_stmt, Entry(pInfo, this_unit.qc_sql_mode));

                ++m_stats.inserts;
                m_stats.size += size;
            }
        }
    }

    void get_stats(QC_CACHE_STATS* pStats)
    {
        *pStats = m_stats;
    }

private:
    struct Entry
    {
        Entry(QC_STMT_INFO* pInfo, qc_sql_mode_t sql_mode)
            : pInfo(pInfo)
            , sql_mode(sql_mode)
        {
        }

        QC_STMT_INFO* pInfo;
        qc_sql_mode_t sql_mode;
    };

    typedef std::unordered_map<std::string, Entry> InfosByStmt;

    void erase(InfosByStmt::iterator& i)
    {
        mxb_assert(i != m_infos.end());

        m_stats.size -= i->first.size();

        mxb_assert(this_unit.classifier);
        this_unit.classifier->qc_info_close(i->second.pInfo);

        m_infos.erase(i);

        ++m_stats.evictions;
    }

    bool erase(const std::string& canonical_stmt)
    {
        bool erased = false;

        auto i = m_infos.find(canonical_stmt);
        mxb_assert(i != m_infos.end());

        if (i != m_infos.end())
        {
            erase(i);
            erased = true;
        }

        return erased;
    }

    void make_space(int64_t required_space)
    {
        int64_t freed_space = 0;

        std::uniform_int_distribution<> dis(0, m_infos.bucket_count() - 1);

        while ((freed_space < required_space) && !m_infos.empty())
        {
            freed_space += evict(dis);
        }
    }

    int64_t evict(std::uniform_int_distribution<>& dis)
    {
        int64_t freed_space = 0;

        int bucket = dis(m_reng);
        mxb_assert((bucket >= 0) && (bucket < static_cast<int>(m_infos.bucket_count())));

        auto i = m_infos.begin(bucket);

        // We just remove the first entry in the bucket. In the general case
        // there will be just one.
        if (i != m_infos.end(bucket))
        {
            freed_space += i->first.size();

            MXB_AT_DEBUG(bool erased = ) erase(i->first);
            mxb_assert(erased);
        }

        return freed_space;
    }

    InfosByStmt        m_infos;
    QC_CACHE_STATS     m_stats;
    std::random_device m_rdev;
    std::mt19937       m_reng;
};

bool use_cached_result()
{
    return this_unit.cache_max_size() != 0;
}

bool has_not_been_parsed(GWBUF* pStmt)
{
    // A GWBUF has not been parsed, if it does not have a parsing info object attached.
    return gwbuf_get_buffer_object_data(pStmt, GWBUF_PARSING_INFO) == nullptr;
}

void info_object_close(void* pData)
{
    mxb_assert(this_unit.classifier);
    this_unit.classifier->qc_info_close(static_cast<QC_STMT_INFO*>(pData));
}


/**
 * @class QCInfoCacheScope
 *
 * QCInfoCacheScope is somewhat like a guard or RAII class that
 * in the constructor
 * - figures out whether the query classification cache should be used,
 * - checks whether the classification result already exists, and
 * - if it does attaches it to the GWBUF
 * and in the destructor
 * - if the query classification result was not already present,
 *   stores the result it in the cache.
 */
class QCInfoCacheScope
{
public:
    QCInfoCacheScope(const QCInfoCacheScope&) = delete;
    QCInfoCacheScope& operator=(const QCInfoCacheScope&) = delete;

    QCInfoCacheScope(GWBUF* pStmt)
        : m_pStmt(pStmt)
    {
        if (use_cached_result() && has_not_been_parsed(m_pStmt))
        {
            m_canonical = mxs::get_canonical(m_pStmt);

            if (modutil_is_SQL_prepare(pStmt))
            {
                // P as in prepare, and appended so as not to cause a
                // need for copying the data.
                m_canonical += ":P";
            }

            QC_STMT_INFO* pInfo = this_thread.pInfo_cache->get(m_canonical);

            if (pInfo)
            {
                gwbuf_add_buffer_object(m_pStmt, GWBUF_PARSING_INFO, pInfo, info_object_close);
                m_canonical.clear();    // Signals that nothing needs to be added in the destructor.
            }
        }
    }

    ~QCInfoCacheScope()
    {
        if (!m_canonical.empty())
        {
            void* pData = gwbuf_get_buffer_object_data(m_pStmt, GWBUF_PARSING_INFO);
            mxb_assert(pData);
            QC_STMT_INFO* pInfo = static_cast<QC_STMT_INFO*>(pData);

            this_thread.pInfo_cache->insert(m_canonical, pInfo);
        }
    }

private:
    GWBUF*      m_pStmt;
    std::string m_canonical;
};
}


bool qc_setup(const QC_CACHE_PROPERTIES* cache_properties,
              qc_sql_mode_t sql_mode,
              const char*   plugin_name,
              const char*   plugin_args)
{
    QC_TRACE();
    mxb_assert(!this_unit.classifier);

    if (!plugin_name || (*plugin_name == 0))
    {
        MXS_NOTICE("No query classifier specified, using default '%s'.", DEFAULT_QC_NAME);
        plugin_name = DEFAULT_QC_NAME;
    }

    int32_t rv = QC_RESULT_ERROR;
    this_unit.classifier = qc_load(plugin_name);

    if (this_unit.classifier)
    {
        rv = this_unit.classifier->qc_setup(sql_mode, plugin_args);

        if (rv == QC_RESULT_OK)
        {
            this_unit.qc_sql_mode = sql_mode;

            int64_t cache_max_size = (cache_properties ? cache_properties->max_size : 0);
            mxb_assert(cache_max_size >= 0);

            if (cache_max_size)
            {
                int64_t size_per_thr = cache_max_size / config_get_global_options()->n_threads;
                MXS_NOTICE("Query classification results are cached and reused. "
                           "Memory used per thread: %s", mxb::to_binary_size(size_per_thr).c_str());
            }
            else
            {
                MXS_NOTICE("Query classification results are not cached.");
            }

            this_unit.set_cache_max_size(cache_max_size);
        }
        else
        {
            qc_unload(this_unit.classifier);
        }
    }

    return (rv == QC_RESULT_OK) ? true : false;
}

bool qc_init(const QC_CACHE_PROPERTIES* cache_properties,
             qc_sql_mode_t sql_mode,
             const char*   plugin_name,
             const char*   plugin_args)
{
    QC_TRACE();

    bool rc = qc_setup(cache_properties, sql_mode, plugin_name, plugin_args);

    if (rc)
    {
        rc = qc_process_init(QC_INIT_BOTH);

        if (rc)
        {
            rc = qc_thread_init(QC_INIT_BOTH);

            if (!rc)
            {
                qc_process_end(QC_INIT_BOTH);
            }
        }
    }

    return rc;
}

void qc_end()
{
    qc_thread_end(QC_INIT_BOTH);
    qc_process_end(QC_INIT_BOTH);
}

bool qc_process_init(uint32_t kind)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    const char* parse_using = getenv(QC_TRX_PARSE_USING);

    if (parse_using)
    {
        if (strcmp(parse_using, "QC_TRX_PARSE_USING_QC") == 0)
        {
            this_unit.qc_trx_parse_using = QC_TRX_PARSE_USING_QC;
            MXS_NOTICE("Transaction detection using QC.");
        }
        else if (strcmp(parse_using, "QC_TRX_PARSE_USING_PARSER") == 0)
        {
            this_unit.qc_trx_parse_using = QC_TRX_PARSE_USING_PARSER;
            MXS_NOTICE("Transaction detection using custom PARSER.");
        }
        else
        {
            MXS_NOTICE("QC_TRX_PARSE_USING set, but the value %s is not known. "
                       "Parsing using QC.",
                       parse_using);
        }
    }

    bool rc = true;

    if (kind & QC_INIT_PLUGIN)
    {
        rc = this_unit.classifier->qc_process_init() == 0;
    }

    return rc;
}

void qc_process_end(uint32_t kind)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    if (kind & QC_INIT_PLUGIN)
    {
        this_unit.classifier->qc_process_end();
    }
}

QUERY_CLASSIFIER* qc_load(const char* plugin_name)
{
    void* module = load_module(plugin_name, MODULE_QUERY_CLASSIFIER);

    if (module)
    {
        MXS_INFO("%s loaded.", plugin_name);
    }
    else
    {
        MXS_ERROR("Could not load %s.", plugin_name);
    }

    return (QUERY_CLASSIFIER*) module;
}

void qc_unload(QUERY_CLASSIFIER* classifier)
{
    // TODO: The module loading/unloading needs an overhaul before we
    // TODO: actually can unload something.
    classifier = NULL;
}

bool qc_thread_init(uint32_t kind)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    bool rc = false;

    if (kind & QC_INIT_SELF)
    {
        mxb_assert(!this_thread.pInfo_cache);
        this_thread.pInfo_cache = new(std::nothrow) QCInfoCache;
        rc = true;
    }
    else
    {
        rc = true;
    }

    if (rc)
    {
        if (kind & QC_INIT_PLUGIN)
        {
            rc = this_unit.classifier->qc_thread_init() == 0;
        }

        if (!rc)
        {
            if (kind & QC_INIT_SELF)
            {
                delete this_thread.pInfo_cache;
                this_thread.pInfo_cache = nullptr;
            }
        }
    }

    return rc;
}

void qc_thread_end(uint32_t kind)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    if (kind & QC_INIT_PLUGIN)
    {
        this_unit.classifier->qc_thread_end();
    }

    if (kind & QC_INIT_SELF)
    {
        delete this_thread.pInfo_cache;
        this_thread.pInfo_cache = nullptr;
    }
}

qc_parse_result_t qc_parse(GWBUF* query, uint32_t collect)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    int32_t result = QC_QUERY_INVALID;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_parse(query, collect, &result);

    return (qc_parse_result_t)result;
}

uint32_t qc_get_type_mask(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    uint32_t type_mask = QUERY_TYPE_UNKNOWN;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_type_mask(query, &type_mask);

    return type_mask;
}

qc_query_op_t qc_get_operation(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    int32_t op = QUERY_OP_UNDEFINED;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_operation(query, &op);

    return (qc_query_op_t)op;
}

char* qc_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    char* name = NULL;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_created_table_name(query, &name);

    return name;
}

bool qc_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    int32_t is_drop_table = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_is_drop_table_query(query, &is_drop_table);

    return (is_drop_table != 0) ? true : false;
}

char** qc_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    char** names = NULL;
    *tblsize = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_table_names(query, fullnames, &names, tblsize);

    return names;
}

void qc_free_table_names(char** names, int tblsize)
{
    if (names)
    {
        for (int i = 0; i < tblsize; i++)
        {
            MXS_FREE(names[i]);
        }

        MXS_FREE(names);
    }
}

char* qc_get_canonical(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    char* rval;

    if (this_unit.classifier->qc_get_canonical)
    {
        this_unit.classifier->qc_get_canonical(query, &rval);
    }
    else
    {
        rval = modutil_get_canonical(query);
    }

    if (rval)
    {
        squeeze_whitespace(rval);
    }

    return rval;
}

bool qc_query_has_clause(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    int32_t has_clause = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_query_has_clause(query, &has_clause);

    return (has_clause != 0) ? true : false;
}

void qc_get_field_info(GWBUF* query, const QC_FIELD_INFO** infos, size_t* n_infos)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    *infos = NULL;

    uint32_t n = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_field_info(query, infos, &n);

    *n_infos = n;
}

void qc_get_function_info(GWBUF* query, const QC_FUNCTION_INFO** infos, size_t* n_infos)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    *infos = NULL;

    uint32_t n = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_function_info(query, infos, &n);

    *n_infos = n;
}

char** qc_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    char** names = NULL;
    *sizep = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_database_names(query, &names, sizep);

    return names;
}

char* qc_get_prepare_name(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    char* name = NULL;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_prepare_name(query, &name);

    return name;
}

GWBUF* qc_get_preparable_stmt(GWBUF* stmt)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    GWBUF* preparable_stmt = NULL;

    QCInfoCacheScope scope(stmt);
    this_unit.classifier->qc_get_preparable_stmt(stmt, &preparable_stmt);

    return preparable_stmt;
}

const char* qc_result_to_string(qc_parse_result_t result)
{
    switch (result)
    {
    case QC_QUERY_INVALID:
        return "QC_QUERY_INVALID";

    case QC_QUERY_TOKENIZED:
        return "QC_QUERY_TOKENIZED";

    case QC_QUERY_PARTIALLY_PARSED:
        return "QC_QUERY_PARTIALLY_PARSED";

    case QC_QUERY_PARSED:
        return "QC_QUERY_PARSED";

    default:
        mxb_assert(!true);
        return "Unknown";
    }
}

const char* qc_op_to_string(qc_query_op_t op)
{
    switch (op)
    {
    case QUERY_OP_UNDEFINED:
        return "QUERY_OP_UNDEFINED";

    case QUERY_OP_ALTER:
        return "QUERY_OP_ALTER";

    case QUERY_OP_CALL:
        return "QUERY_OP_CALL";

    case QUERY_OP_CHANGE_DB:
        return "QUERY_OP_CHANGE_DB";

    case QUERY_OP_CREATE:
        return "QUERY_OP_CREATE";

    case QUERY_OP_DELETE:
        return "QUERY_OP_DELETE";

    case QUERY_OP_DROP:
        return "QUERY_OP_DROP";

    case QUERY_OP_EXPLAIN:
        return "QUERY_OP_EXPLAIN";

    case QUERY_OP_GRANT:
        return "QUERY_OP_GRANT";

    case QUERY_OP_INSERT:
        return "QUERY_OP_INSERT";

    case QUERY_OP_LOAD:
        return "QUERY_OP_LOAD";

    case QUERY_OP_LOAD_LOCAL:
        return "QUERY_OP_LOAD_LOCAL";

    case QUERY_OP_REVOKE:
        return "QUERY_OP_REVOKE";

    case QUERY_OP_SELECT:
        return "QUERY_OP_SELECT";

    case QUERY_OP_SHOW:
        return "QUERY_OP_SHOW";

    case QUERY_OP_TRUNCATE:
        return "QUERY_OP_TRUNCATE";

    case QUERY_OP_UPDATE:
        return "QUERY_OP_UPDATE";

    default:
        return "UNKNOWN_QUERY_OP";
    }
}

struct type_name_info type_to_type_name_info(qc_query_type_t type)
{
    struct type_name_info info;

    switch (type)
    {
    case QUERY_TYPE_UNKNOWN:
        {
            static const char name[] = "QUERY_TYPE_UNKNOWN";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_LOCAL_READ:
        {
            static const char name[] = "QUERY_TYPE_LOCAL_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READ:
        {
            static const char name[] = "QUERY_TYPE_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_WRITE:
        {
            static const char name[] = "QUERY_TYPE_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_MASTER_READ:
        {
            static const char name[] = "QUERY_TYPE_MASTER_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SESSION_WRITE:
        {
            static const char name[] = "QUERY_TYPE_SESSION_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_USERVAR_WRITE:
        {
            static const char name[] = "QUERY_TYPE_USERVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_USERVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_USERVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SYSVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_SYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    /** Not implemented yet */
    // case QUERY_TYPE_SYSVAR_WRITE:
    case QUERY_TYPE_GSYSVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_GSYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_GSYSVAR_WRITE:
        {
            static const char name[] = "QUERY_TYPE_GSYSVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_BEGIN_TRX:
        {
            static const char name[] = "QUERY_TYPE_BEGIN_TRX";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_ENABLE_AUTOCOMMIT:
        {
            static const char name[] = "QUERY_TYPE_ENABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_DISABLE_AUTOCOMMIT:
        {
            static const char name[] = "QUERY_TYPE_DISABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_ROLLBACK:
        {
            static const char name[] = "QUERY_TYPE_ROLLBACK";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_COMMIT:
        {
            static const char name[] = "QUERY_TYPE_COMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_PREPARE_NAMED_STMT:
        {
            static const char name[] = "QUERY_TYPE_PREPARE_NAMED_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_PREPARE_STMT:
        {
            static const char name[] = "QUERY_TYPE_PREPARE_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_EXEC_STMT:
        {
            static const char name[] = "QUERY_TYPE_EXEC_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_CREATE_TMP_TABLE:
        {
            static const char name[] = "QUERY_TYPE_CREATE_TMP_TABLE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READ_TMP_TABLE:
        {
            static const char name[] = "QUERY_TYPE_READ_TMP_TABLE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SHOW_DATABASES:
        {
            static const char name[] = "QUERY_TYPE_SHOW_DATABASES";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SHOW_TABLES:
        {
            static const char name[] = "QUERY_TYPE_SHOW_TABLES";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_DEALLOC_PREPARE:
        {
            static const char name[] = "QUERY_TYPE_DEALLOC_PREPARE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    default:
        {
            static const char name[] = "UNKNOWN_QUERY_TYPE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;
    }

    return info;
}


const char* qc_type_to_string(qc_query_type_t type)
{
    return type_to_type_name_info(type).name;
}

static const qc_query_type_t QUERY_TYPES[] =
{
    /* Excluded by design */
    // QUERY_TYPE_UNKNOWN,
    QUERY_TYPE_LOCAL_READ,
    QUERY_TYPE_READ,
    QUERY_TYPE_WRITE,
    QUERY_TYPE_MASTER_READ,
    QUERY_TYPE_SESSION_WRITE,
    QUERY_TYPE_USERVAR_WRITE,
    QUERY_TYPE_USERVAR_READ,
    QUERY_TYPE_SYSVAR_READ,
    /** Not implemented yet */
    // QUERY_TYPE_SYSVAR_WRITE,
    QUERY_TYPE_GSYSVAR_READ,
    QUERY_TYPE_GSYSVAR_WRITE,
    QUERY_TYPE_BEGIN_TRX,
    QUERY_TYPE_ENABLE_AUTOCOMMIT,
    QUERY_TYPE_DISABLE_AUTOCOMMIT,
    QUERY_TYPE_ROLLBACK,
    QUERY_TYPE_COMMIT,
    QUERY_TYPE_PREPARE_NAMED_STMT,
    QUERY_TYPE_PREPARE_STMT,
    QUERY_TYPE_EXEC_STMT,
    QUERY_TYPE_CREATE_TMP_TABLE,
    QUERY_TYPE_READ_TMP_TABLE,
    QUERY_TYPE_SHOW_DATABASES,
    QUERY_TYPE_SHOW_TABLES,
    QUERY_TYPE_DEALLOC_PREPARE,
};

static const int N_QUERY_TYPES = sizeof(QUERY_TYPES) / sizeof(QUERY_TYPES[0]);
static const int QUERY_TYPE_MAX_LEN = 29;   // strlen("QUERY_TYPE_PREPARE_NAMED_STMT");

char* qc_typemask_to_string(uint32_t types)
{
    int len = 0;

    // First calculate how much space will be needed.
    for (int i = 0; i < N_QUERY_TYPES; ++i)
    {
        if (types & QUERY_TYPES[i])
        {
            if (len != 0)
            {
                ++len;      // strlen("|");
            }

            len += QUERY_TYPE_MAX_LEN;
        }
    }

    ++len;

    // Then make one allocation and build the string.
    char* s = (char*) MXS_MALLOC(len);

    if (s)
    {
        if (len > 1)
        {
            char* p = s;

            for (int i = 0; i < N_QUERY_TYPES; ++i)
            {
                qc_query_type_t type = QUERY_TYPES[i];

                if (types & type)
                {
                    if (p != s)
                    {
                        strcpy(p, "|");
                        ++p;
                    }

                    struct type_name_info info = type_to_type_name_info(type);

                    strcpy(p, info.name);
                    p += info.name_len;
                }
            }
        }
        else
        {
            *s = 0;
        }
    }

    return s;
}

static uint32_t qc_get_trx_type_mask_using_qc(GWBUF* stmt)
{
    uint32_t type_mask = qc_get_type_mask(stmt);

    if (qc_query_is_type(type_mask, QUERY_TYPE_WRITE)
        && qc_query_is_type(type_mask, QUERY_TYPE_COMMIT))
    {
        // This is a commit reported for "CREATE TABLE...",
        // "DROP TABLE...", etc. that cause an implicit commit.
        type_mask = 0;
    }
    else
    {
        // Only START TRANSACTION can be explicitly READ or WRITE.
        if (!(type_mask & QUERY_TYPE_BEGIN_TRX))
        {
            // So, strip them away for everything else.
            type_mask &= ~(QUERY_TYPE_WRITE | QUERY_TYPE_READ);
        }

        // Then leave only the bits related to transaction and
        // autocommit state.
        type_mask &= (QUERY_TYPE_BEGIN_TRX
                      | QUERY_TYPE_WRITE
                      | QUERY_TYPE_READ
                      | QUERY_TYPE_COMMIT
                      | QUERY_TYPE_ROLLBACK
                      | QUERY_TYPE_ENABLE_AUTOCOMMIT
                      | QUERY_TYPE_DISABLE_AUTOCOMMIT);
    }

    return type_mask;
}

static uint32_t qc_get_trx_type_mask_using_parser(GWBUF* stmt)
{
    maxscale::TrxBoundaryParser parser;

    return parser.type_mask_of(stmt);
}

uint32_t qc_get_trx_type_mask_using(GWBUF* stmt, qc_trx_parse_using_t use)
{
    uint32_t type_mask = 0;

    switch (use)
    {
    case QC_TRX_PARSE_USING_QC:
        type_mask = qc_get_trx_type_mask_using_qc(stmt);
        break;

    case QC_TRX_PARSE_USING_PARSER:
        type_mask = qc_get_trx_type_mask_using_parser(stmt);
        break;

    default:
        mxb_assert(!true);
    }

    return type_mask;
}

uint32_t qc_get_trx_type_mask(GWBUF* stmt)
{
    return qc_get_trx_type_mask_using(stmt, this_unit.qc_trx_parse_using);
}

void qc_set_server_version(uint64_t version)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    this_unit.classifier->qc_set_server_version(version);
}

uint64_t qc_get_server_version()
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    uint64_t version;

    this_unit.classifier->qc_get_server_version(&version);

    return version;
}

qc_sql_mode_t qc_get_sql_mode()
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    return this_unit.qc_sql_mode;
}

void qc_set_sql_mode(qc_sql_mode_t sql_mode)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    int32_t rv = this_unit.classifier->qc_set_sql_mode(sql_mode);
    mxb_assert(rv == QC_RESULT_OK);

    if (rv == QC_RESULT_OK)
    {
        this_unit.qc_sql_mode = sql_mode;
    }
}

void qc_get_cache_properties(QC_CACHE_PROPERTIES* properties)
{
    properties->max_size = this_unit.cache_max_size();
}

bool qc_set_cache_properties(const QC_CACHE_PROPERTIES* properties)
{
    bool rv = false;

    if (properties->max_size >= 0)
    {
        this_unit.set_cache_max_size(properties->max_size);
        rv = true;
    }
    else
    {
        MXS_ERROR("Ignoring attempt to set size of query classifier "
                  "cache to a negative value: %" PRIi64 ".",
                  properties->max_size);
    }

    return rv;
}

bool qc_get_cache_stats(QC_CACHE_STATS* pStats)
{
    QC_TRACE();

    bool rv = false;

    QCInfoCache* pInfo_cache = this_thread.pInfo_cache;

    if (pInfo_cache && use_cached_result())
    {
        pInfo_cache->get_stats(pStats);
        rv = true;
    }

    return rv;
}

json_t* qc_get_cache_stats_as_json()
{
    QC_CACHE_STATS stats = {};
    qc_get_cache_stats(&stats);

    json_t* pStats = json_object();
    json_object_set_new(pStats, "size", json_integer(stats.size));
    json_object_set_new(pStats, "inserts", json_integer(stats.inserts));
    json_object_set_new(pStats, "hits", json_integer(stats.hits));
    json_object_set_new(pStats, "misses", json_integer(stats.misses));
    json_object_set_new(pStats, "evictions", json_integer(stats.evictions));

    return pStats;
}

std::unique_ptr<json_t> qc_as_json(const char* zHost)
{
    json_t* pParams = json_object();
    json_object_set_new(pParams, CN_CACHE_SIZE, json_integer(this_unit.cache_max_size()));

    json_t* pAttributes = json_object();
    json_object_set_new(pAttributes, CN_PARAMETERS, pParams);

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(CN_QUERY_CLASSIFIER));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_QUERY_CLASSIFIER));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC, pSelf));
}

namespace
{

json_t* get_params(json_t* pJson)
{
    json_t* pParams = mxs_json_pointer(pJson, MXS_JSON_PTR_PARAMETERS);

    if (pParams && json_is_object(pParams))
    {
        if (!runtime_is_count_or_null(pParams, CN_CACHE_SIZE))
        {
            pParams = nullptr;
        }
    }

    return pParams;
}
}

bool qc_alter_from_json(json_t* pJson)
{
    bool rv = false;

    json_t* pParams = get_params(pJson);

    if (pParams)
    {
        rv = true;

        QC_CACHE_PROPERTIES cache_properties;
        qc_get_cache_properties(&cache_properties);

        json_t* pValue;

        if ((pValue = mxs_json_pointer(pParams, CN_CACHE_SIZE)))
        {
            cache_properties.max_size = json_integer_value(pValue);
            // If runtime_is_count_or_null() did its job, then we will not
            // get here if the value is negative.
            mxb_assert(cache_properties.max_size >= 0);
        }

        if (rv)
        {
            MXB_AT_DEBUG(bool set = ) qc_set_cache_properties(&cache_properties);
            mxb_assert(set);
        }
    }

    return rv;
}

namespace
{

void append_field_info(json_t* pParent,
                       const char* zName,
                       const QC_FIELD_INFO* begin, const QC_FIELD_INFO* end)
{
    json_t* pFields = json_array();

    std::for_each(begin, end, [pFields](const QC_FIELD_INFO& info) {
                      std::string name;

                      if (info.database)
                      {
                          name += info.database;
                          name += '.';
                          mxb_assert(info.table);
                      }

                      if (info.table)
                      {
                          name += info.table;
                          name += '.';
                      }

                      mxb_assert(info.column);

                      name += info.column;

                      json_array_append_new(pFields, json_string(name.c_str()));
                  });

    json_object_set_new(pParent, zName, pFields);
}

void append_field_info(json_t* pParams, GWBUF* pBuffer)
{
    const QC_FIELD_INFO* begin;
    size_t n;
    qc_get_field_info(pBuffer, &begin, &n);

    append_field_info(pParams, CN_FIELDS, begin, begin + n);
}

void append_function_info(json_t* pParams, GWBUF* pBuffer)
{
    json_t* pFunctions = json_array();

    const QC_FUNCTION_INFO* begin;
    size_t n;
    qc_get_function_info(pBuffer, &begin, &n);

    std::for_each(begin, begin + n, [pFunctions](const QC_FUNCTION_INFO& info) {
                      json_t* pFunction = json_object();

                      json_object_set_new(pFunction, CN_NAME, json_string(info.name));

                      append_field_info(pFunction, CN_ARGUMENTS, info.fields, info.fields + info.n_fields);

                      json_array_append_new(pFunctions, pFunction);
                  });

    json_object_set_new(pParams, CN_FUNCTIONS, pFunctions);
}
}

std::unique_ptr<json_t> qc_classify_as_json(const char* zHost, const std::string& statement)
{
    json_t* pParams = json_object();

    std::unique_ptr<GWBUF> sBuffer(modutil_create_query(statement.c_str()));
    GWBUF* pBuffer = sBuffer.get();

    qc_parse_result result = qc_parse(pBuffer, QC_COLLECT_ALL);

    json_object_set_new(pParams, CN_PARSE_RESULT, json_string(qc_result_to_string(result)));

    if (result != QC_QUERY_INVALID)
    {
        char* zType_mask = qc_typemask_to_string(qc_get_type_mask(pBuffer));
        json_object_set_new(pParams, CN_TYPE_MASK, json_string(zType_mask));
        MXS_FREE(zType_mask);

        json_object_set_new(pParams, CN_OPERATION, json_string(qc_op_to_string(qc_get_operation(pBuffer))));
        bool has_clause = qc_query_has_clause(pBuffer);
        json_object_set_new(pParams, CN_HAS_WHERE_CLAUSE, json_boolean(has_clause));

        append_field_info(pParams, pBuffer);
        append_function_info(pParams, pBuffer);
    }

    json_t* pAttributes = json_object();
    json_object_set_new(pAttributes, CN_PARAMETERS, pParams);

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_CLASSIFY, pSelf));
}
