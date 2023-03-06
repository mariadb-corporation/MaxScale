/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cachingparser.hh>
#include <atomic>
#include <map>
#include <random>
#include <maxscale/buffer.hh>
// TODO: Remove mariadb dependency.
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/routingworker.hh>
// TODO: Remove this dependency.
#include "../modules/protocol/MariaDB/trxboundaryparser.hh"

namespace
{

const char QC_TRX_PARSE_USING[] = "QC_TRX_PARSE_USING";

class ThisUnit
{
public:
    ThisUnit()
        : qc_trx_parse_using(QC_TRX_PARSE_USING_PARSER)
        , m_cache_max_size(std::numeric_limits<int64_t>::max())
    {
    }

    ThisUnit(const ThisUnit&) = delete;
    ThisUnit& operator=(const ThisUnit&) = delete;

    qc_trx_parse_using_t qc_trx_parse_using;

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

ThisUnit this_unit;

class QCInfoCache;

thread_local struct
{
    QCInfoCache* pInfo_cache { nullptr };
    uint32_t     options { 0 };
    bool         use_cache { true };
} this_thread;


bool use_cached_result()
{
    return this_unit.cache_max_size() != 0 && this_thread.use_cache;
}

bool has_not_been_parsed(GWBUF* pStmt)
{
    // A GWBUF has not been parsed, if it does not have a parsing info object attached.
    return pStmt->get_classifier_data_ptr() == nullptr;
}

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
    }

    QC_STMT_INFO* peek(std::string_view canonical_stmt) const
    {
        auto i = m_infos.find(canonical_stmt);

        return i != m_infos.end() ? i->second.sInfo.get() : nullptr;
    }

    std::shared_ptr<QC_STMT_INFO> get(QUERY_CLASSIFIER* pClassifier, std::string_view canonical_stmt)
    {
        std::shared_ptr<QC_STMT_INFO> sInfo;
        qc_sql_mode_t sql_mode;
        pClassifier->get_sql_mode(&sql_mode);

        auto i = m_infos.find(canonical_stmt);

        if (i != m_infos.end())
        {
            Entry& entry = i->second;

            if ((entry.sql_mode == sql_mode)
                && (entry.options == this_thread.options))
            {
                sInfo = entry.sInfo;

                ++entry.hits;
                ++m_stats.hits;
            }
            else
            {
                // If the sql_mode or options has changed, we discard the existing result.
                erase(i);

                ++m_stats.misses;
            }
        }
        else
        {
            ++m_stats.misses;
        }

        return sInfo;
    }

    void insert(QUERY_CLASSIFIER* pClassifier, std::string_view canonical_stmt, std::shared_ptr<QC_STMT_INFO> sInfo)
    {
        mxb_assert(peek(canonical_stmt) == nullptr);

        // 0xffffff is the maximum packet size, 4 is for packet header and 1 is for command byte. These are
        // MariaDB/MySQL protocol specific values that are also defined in <maxscale/protocol/mysql.h> but
        // should not be exposed to the core.
        constexpr int64_t max_entry_size = 0xffffff - 5;

        // RoutingWorker::nRunning() and not Config::n_threads, as the former tells how many
        // threads are currently running and the latter how many they eventually will be.
        // When increasing there will not be a difference, but when decreasing there will be.
        int64_t cache_max_size = this_unit.cache_max_size() / mxs::RoutingWorker::nRunning();

        /** Because some queries cause much more memory to be used than can be measured,
         *  the limit is reduced here. In the future the cache entries will be changed so
         *  that memory fragmentation is minimized.
         */
        cache_max_size *= 0.65;

        int64_t size = entry_size(sInfo.get());

        if (size < max_entry_size && size <= cache_max_size)
        {
            int64_t required_space = (m_stats.size + size) - cache_max_size;

            if (required_space > 0)
            {
                make_space(required_space);
            }

            if (m_stats.size + size <= cache_max_size)
            {
                qc_sql_mode_t sql_mode;
                pClassifier->get_sql_mode(&sql_mode);

                m_infos.emplace(canonical_stmt,
                                Entry(pClassifier, std::move(sInfo), sql_mode, this_thread.options));

                ++m_stats.inserts;
                m_stats.size += size;
            }
        }
    }

    void update_total_size(int32_t delta)
    {
        m_stats.size += delta;
    }

    void get_stats(QC_CACHE_STATS* pStats)
    {
        *pStats = m_stats;
    }

    void get_state(std::map<std::string, QC_CACHE_ENTRY>& state) const
    {
        for (const auto& info : m_infos)
        {
            std::string stmt = std::string(info.first);
            const Entry& entry = info.second;

            auto it = state.find(stmt);

            if (it == state.end())
            {
                QC_CACHE_ENTRY e {};

                e.hits = entry.hits;
                e.result = entry.pClassifier->get_result_from_info(entry.sInfo.get());

                state.insert(std::make_pair(stmt, e));
            }
            else
            {
                QC_CACHE_ENTRY& e = it->second;

                e.hits += entry.hits;
#if defined (SS_DEBUG)
                QC_STMT_RESULT result = entry.pClassifier->get_result_from_info(entry.sInfo.get());

                mxb_assert(e.result.status == result.status);
                mxb_assert(e.result.type_mask == result.type_mask);
                mxb_assert(e.result.op == result.op);
#endif
            }
        }
    }

    int64_t clear()
    {
        int64_t rv = 0;

        for (auto& kv : m_infos)
        {
            rv += entry_size(kv.second.sInfo.get());
        }

        m_infos.clear();

        return rv;
    }

private:
    struct Entry
    {
        Entry(QUERY_CLASSIFIER* pClassifier, std::shared_ptr<QC_STMT_INFO> sInfo, qc_sql_mode_t sql_mode, uint32_t options)
            : pClassifier(pClassifier)
            , sInfo(std::move(sInfo))
            , sql_mode(sql_mode)
            , options(options)
            , hits(0)
        {
        }

        QUERY_CLASSIFIER*             pClassifier;
        std::shared_ptr<QC_STMT_INFO> sInfo;
        qc_sql_mode_t                 sql_mode;
        uint32_t                      options;
        int64_t                       hits;
    };

    typedef std::unordered_map<std::string_view, Entry> InfosByStmt;

    int64_t entry_size(const QC_STMT_INFO* pInfo)
    {
        const int64_t map_entry_overhead = 4 * sizeof(void *);
        const int64_t constant_overhead = sizeof(std::string_view) + sizeof(Entry) + map_entry_overhead;

        return constant_overhead + pInfo->size();
    }

    int64_t entry_size(const InfosByStmt::value_type& entry)
    {
        return entry_size(entry.second.sInfo.get());
    }

    void erase(InfosByStmt::iterator& i)
    {
        mxb_assert(i != m_infos.end());

        m_stats.size -= entry_size(*i);

        m_infos.erase(i);

        ++m_stats.evictions;
    }

    bool erase(std::string_view canonical_stmt)
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
            freed_space += entry_size(*i);

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

    QCInfoCacheScope(QUERY_CLASSIFIER* pClassifier, GWBUF* pStmt)
        : m_pClassifier(pClassifier)
        , m_pStmt(pStmt)
    {
        auto pInfo = static_cast<QC_STMT_INFO*>(m_pStmt->get_classifier_data_ptr());
        m_info_size_before = pInfo ? pInfo->size() : 0;

        if (use_cached_result() && has_not_been_parsed(m_pStmt))
        {
            m_canonical = m_pStmt->get_canonical(); // Not from the QC, but from GWBUF.

            // TODO: Remove mariadb dependency.
            if (mariadb::is_com_prepare(*pStmt))
            {
                // P as in prepare, and appended so as not to cause a
                // need for copying the data.
                m_canonical += ":P";
            }

            std::shared_ptr<QC_STMT_INFO> sInfo = this_thread.pInfo_cache->get(m_pClassifier, m_canonical);
            if (sInfo)
            {
                m_info_size_before = sInfo->size();
                m_pStmt->set_classifier_data(std::move(sInfo));
                m_canonical.clear();    // Signals that nothing needs to be added in the destructor.
            }
        }
    }

    ~QCInfoCacheScope()
    {
        bool exclude = exclude_from_cache();

        if (!m_canonical.empty() && !exclude)
        {   // Cache for the first time
            auto sInfo = m_pStmt->get_classifier_data();
            mxb_assert(sInfo);

            // Now from QC and this will have the trailing ":P" in case the GWBUF
            // contained a COM_STMT_PREPARE.
            std::string_view canonical = m_pClassifier->info_get_canonical(sInfo.get());
            mxb_assert(m_canonical == canonical);

            this_thread.pInfo_cache->insert(m_pClassifier, canonical, std::move(sInfo));
        }
        else if (!exclude)
        {   // The size might have changed
            auto pInfo = m_pStmt->get_classifier_data_ptr();
            auto info_size_after = pInfo ? pInfo->size() : 0;

            if (m_info_size_before != info_size_after)
            {
                mxb_assert(m_info_size_before < info_size_after);
                this_thread.pInfo_cache->update_total_size(info_size_after - m_info_size_before);
            }
        }
    }

private:
    QUERY_CLASSIFIER* m_pClassifier;
    GWBUF*            m_pStmt;
    std::string       m_canonical;
    size_t            m_info_size_before;

    bool exclude_from_cache() const
    {
        constexpr const int is_autocommit = QUERY_TYPE_ENABLE_AUTOCOMMIT | QUERY_TYPE_DISABLE_AUTOCOMMIT;
        uint32_t type_mask = QUERY_TYPE_UNKNOWN;
        m_pClassifier->get_type_mask(m_pStmt, &type_mask);
        return (type_mask & is_autocommit) != 0;
    }
};

}

namespace maxscale
{

//static
void CachingParser::init()
{
    const char* zParse_using = getenv(QC_TRX_PARSE_USING);

    if (zParse_using)
    {
        if (strcmp(zParse_using, "QC_TRX_PARSE_USING_QC") == 0)
        {
            this_unit.qc_trx_parse_using = QC_TRX_PARSE_USING_QC;
            MXB_NOTICE("Transaction detection using QC.");
        }
        else if (strcmp(zParse_using, "QC_TRX_PARSE_USING_PARSER") == 0)
        {
            this_unit.qc_trx_parse_using = QC_TRX_PARSE_USING_PARSER;
            MXB_NOTICE("Transaction detection using custom PARSER.");
        }
        else
        {
            MXB_NOTICE("QC_TRX_PARSE_USING set, but the value %s is not known. "
                       "Parsing using QC.",
                       zParse_using);
        }
    }
}

//static
void CachingParser::thread_init()
{
    mxb_assert(!this_thread.pInfo_cache);
    this_thread.pInfo_cache = new QCInfoCache;
}

//static
void CachingParser::thread_finish()
{
    mxb_assert(this_thread.pInfo_cache);
    delete this_thread.pInfo_cache;
    this_thread.pInfo_cache = nullptr;
}

//static
bool CachingParser::set_properties(const QC_CACHE_PROPERTIES& properties)
{
    bool rv = false;

    if (properties.max_size >= 0)
    {
        if (properties.max_size == 0)
        {
            MXB_NOTICE("Query classifier cache disabled.");
        }

        this_unit.set_cache_max_size(properties.max_size);
        rv = true;
    }
    else
    {
        MXB_ERROR("Ignoring attempt to set size of query classifier "
                  "cache to a negative value: %" PRIi64 ".",
                  properties.max_size);
    }

    return rv;
}

//static
void CachingParser::get_properties(QC_CACHE_PROPERTIES* pProperties)
{
    pProperties->max_size = this_unit.cache_max_size();
}

//static
int64_t CachingParser::clear_thread_cache()
{
    int64_t rv = 0;
    QCInfoCache* pCache = this_thread.pInfo_cache;

    if (pCache)
    {
        rv = pCache->clear();
    }

    return rv;
}

//static
void CachingParser::get_thread_cache_state(std::map<std::string, QC_CACHE_ENTRY>& state)
{
    QCInfoCache* pCache = this_thread.pInfo_cache;

    if (pCache)
    {
        pCache->get_state(state);
    }
}

//static
bool CachingParser::get_thread_cache_stats(QC_CACHE_STATS* pStats)
{
    bool rv = false;

    QCInfoCache* pInfo_cache = this_thread.pInfo_cache;

    if (pInfo_cache && use_cached_result())
    {
        pInfo_cache->get_stats(pStats);
        rv = true;
    }

    return rv;
}

//static
void CachingParser::set_thread_cache_enabled(bool enabled)
{
    this_thread.use_cache = enabled;
}

QUERY_CLASSIFIER& CachingParser::classifier() const
{
    return m_classifier;
}

qc_parse_result_t CachingParser::parse(GWBUF* pStmt, uint32_t collect) const
{
    int32_t result = QC_QUERY_INVALID;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.parse(pStmt, collect, &result);

    return (qc_parse_result_t)result;
}

std::string_view CachingParser::get_created_table_name(GWBUF* query) const
{
    std::string_view name;

    QCInfoCacheScope scope(&m_classifier, query);
    m_classifier.get_created_table_name(query, &name);

    return name;
}

CachingParser::DatabaseNames CachingParser::get_database_names(GWBUF* pStmt) const
{
    std::vector<std::string_view> names;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_database_names(pStmt, &names);

    return names;
}

void CachingParser::get_field_info(GWBUF* pStmt,
                                   const QC_FIELD_INFO** ppInfos,
                                   size_t* pnInfos) const
{
    *ppInfos = NULL;

    uint32_t n = 0;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_field_info(pStmt, ppInfos, &n);

    *pnInfos = n;
}

void CachingParser::get_function_info(GWBUF* pStmt,
                                      const QC_FUNCTION_INFO** ppInfos,
                                      size_t* pnInfos) const
{
    *ppInfos = NULL;

    uint32_t n = 0;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_function_info(pStmt, ppInfos, &n);

    *pnInfos = n;

}

QC_KILL CachingParser::get_kill_info(GWBUF* query) const
{
    QC_KILL rval;

    QCInfoCacheScope scope(&m_classifier, query);
    m_classifier.get_kill_info(query, &rval);

    return rval;
}

qc_query_op_t CachingParser::get_operation(GWBUF* pStmt) const
{
    int32_t op = QUERY_OP_UNDEFINED;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_operation(pStmt, &op);

    return (qc_query_op_t)op;
}

uint32_t CachingParser::get_options() const
{
    return m_classifier.get_options();
}

GWBUF* CachingParser::get_preparable_stmt(GWBUF* pStmt) const
{
    GWBUF* pPreparable_stmt = NULL;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_preparable_stmt(pStmt, &pPreparable_stmt);

    return pPreparable_stmt;
}

std::string_view CachingParser::get_prepare_name(GWBUF* pStmt) const
{
    std::string_view name;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_prepare_name(pStmt, &name);

    return name;
}

uint64_t CachingParser::get_server_version() const
{
    uint64_t version;
    m_classifier.get_server_version(&version);

    return version;
}

qc_sql_mode_t CachingParser::get_sql_mode() const
{
    qc_sql_mode_t sql_mode = QC_SQL_MODE_DEFAULT;
    m_classifier.get_sql_mode(&sql_mode);

    return sql_mode;
}

CachingParser::TableNames CachingParser::get_table_names(GWBUF* pStmt) const
{
    std::vector<QcTableName> names;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_table_names(pStmt, &names);

    return names;
}

namespace
{

uint32_t get_trx_type_mask_using_parser(GWBUF* pStmt)
{
    maxscale::TrxBoundaryParser parser;

    return parser.type_mask_of(pStmt);
}

uint32_t get_trx_type_mask_using_qc(QUERY_CLASSIFIER& m_classifier, GWBUF* pStmt)
{
    uint32_t type_mask;
    m_classifier.get_type_mask(pStmt, &type_mask);

    if (Parser::type_mask_contains(type_mask, QUERY_TYPE_WRITE)
        && Parser::type_mask_contains(type_mask, QUERY_TYPE_COMMIT))
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
                      | QUERY_TYPE_DISABLE_AUTOCOMMIT
                      | QUERY_TYPE_READONLY
                      | QUERY_TYPE_READWRITE
                      | QUERY_TYPE_NEXT_TRX);
    }

    return type_mask;
}

}

uint32_t CachingParser::get_trx_type_mask(GWBUF* pStmt) const
{
    return get_trx_type_mask_using_parser(pStmt);
}

uint32_t CachingParser::get_trx_type_mask_using(GWBUF* pStmt, qc_trx_parse_using_t use) const
{
    uint32_t type_mask = 0;

    switch (use)
    {
    case QC_TRX_PARSE_USING_QC:
        type_mask = get_trx_type_mask_using_qc(m_classifier, pStmt);
        break;

    case QC_TRX_PARSE_USING_PARSER:
        type_mask = get_trx_type_mask_using_parser(pStmt);
        break;

    default:
        mxb_assert(!true);
    }

    return type_mask;
}

uint32_t CachingParser::get_type_mask(GWBUF* pStmt) const
{
    uint32_t type_mask = QUERY_TYPE_UNKNOWN;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.get_type_mask(pStmt, &type_mask);

    return type_mask;
}

bool CachingParser::is_drop_table_query(GWBUF* pStmt) const
{
    int32_t is_drop_table = 0;

    QCInfoCacheScope scope(&m_classifier, pStmt);
    m_classifier.is_drop_table_query(pStmt, &is_drop_table);

    return (is_drop_table != 0) ? true : false;
}

bool CachingParser::set_options(uint32_t options)
{
    int32_t rv = m_classifier.set_options(options);

    if (rv == QC_RESULT_OK)
    {
        this_thread.options = options;
    }

    return rv == QC_RESULT_OK;
}

void CachingParser::set_sql_mode(qc_sql_mode_t sql_mode)
{
    m_classifier.set_sql_mode(sql_mode);
}

void CachingParser::set_server_version(uint64_t version)
{
    m_classifier.set_server_version(version);
}

}
