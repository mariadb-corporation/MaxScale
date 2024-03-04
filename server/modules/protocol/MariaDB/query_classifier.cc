/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/query_classifier.hh>

#include <inttypes.h>
#include <algorithm>
#include <atomic>
#include <random>
#include <unordered_map>
#include <maxbase/alloc.hh>
#include <maxbase/format.hh>
#include <maxbase/pretty_print.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/json_api.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/modutil.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/buffer.hh>
#include <maxsimd/canonical.hh>

#include "trxboundaryparser.hh"

// #define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined (QC_TRACE_ENABLED)
#define QC_TRACE() MXB_NOTICE(__func__)
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
const char CN_ARGUMENTS[] = "arguments";
const char CN_CACHE[] = "cache";
const char CN_CACHE_SIZE[] = "cache_size";
const char CN_CLASSIFICATION[] = "classification";
const char CN_CLASSIFY[] = "classify";
const char CN_FIELDS[] = "fields";
const char CN_FUNCTIONS[] = "functions";
const char CN_HAS_WHERE_CLAUSE[] = "has_where_clause";
const char CN_HITS[] = "hits";
const char CN_OPERATION[] = "operation";
const char CN_PARSE_RESULT[] = "parse_result";
const char CN_TYPE_MASK[] = "type_mask";
const char CN_CANONICAL[] = "canonical";

class ThisUnit
{
public:
    ThisUnit()
        : classifier(nullptr)
        , qc_trx_parse_using(QC_TRX_PARSE_USING_PARSER)
        , m_cache_max_size(std::numeric_limits<int64_t>::max())
    {
    }

    ThisUnit(const ThisUnit&) = delete;
    ThisUnit& operator=(const ThisUnit&) = delete;

    QUERY_CLASSIFIER*    classifier;
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

static ThisUnit this_unit;

class QCInfoCache;

static thread_local struct
{
    QCInfoCache*     pInfo_cache { nullptr };
    uint32_t         options { 0 };
    bool             use_cache { true };
    bool             size_being_adjusted { false };
} this_thread;

/**
 * Returns const references to top N values in the range [it, end)
 *
 * TODO: Move this into a separate header if it's needed elsewhere.
 *
 * @param it   Start of the range
 * @param end  Past-the-end of the range
 * @param n    How many values to return
 * @param comp The comparison function that returns true if the first value is less than the second value.
 *
 * @return Top N values sorted according to comp.
 */
template<class Iter, class Comp,
         typename Reference = std::reference_wrapper<
             const std::remove_cv_t<typename std::iterator_traits<Iter>::value_type>>
         >
std::vector<Reference> limit_n(Iter it, Iter end, size_t n, Comp comp)
{
    std::vector<Reference> entries(it, end);
    auto middle = entries.begin() + std::min(n, entries.size());

    std::partial_sort(entries.begin(), middle, entries.end(), [&](const Reference& a, const Reference& b){
        return comp(a.get(), b.get());
    });

    return std::vector<Reference>(entries.begin(), middle);
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
        , m_cache_max_size(QCInfoCache::thread_cache_max_size())
    {
        memset(&m_stats, 0, sizeof(m_stats));
    }

    ~QCInfoCache()
    {
        mxb_assert(this_unit.classifier);
    }

    static int64_t thread_cache_max_size()
    {
        // RoutingWorker::nRunning() and not Config::n_threads, as the former tells how many
        // threads are currently running and the latter how many they eventually will be.
        // When increasing there will not be a difference, but when decreasing there will be.
        int nRunning = mxs::RoutingWorker::nRunning();
        int64_t cache_max_size = this_unit.cache_max_size() / (nRunning != 0 ? nRunning : 1);

        /** Because some queries cause much more memory to be used than can be measured,
         *  the limit is reduced here. In the future the cache entries will be changed so
         *  that memory fragmentation is minimized.
         */
        cache_max_size *= 0.65;

        return cache_max_size;
    }

    int64_t cache_max_size() const
    {
        return m_cache_max_size;
    }

    void update_cache_max_size()
    {
        m_cache_max_size = QCInfoCache::thread_cache_max_size();
    }

    QC_STMT_INFO* peek(std::string_view canonical_stmt) const
    {
        auto i = m_infos.find(canonical_stmt);

        return i != m_infos.end() ? i->second.sInfo.get() : nullptr;
    }

    std::shared_ptr<QC_STMT_INFO> get(std::string_view canonical_stmt)
    {
        std::shared_ptr<QC_STMT_INFO> sInfo;
        qc_sql_mode_t sql_mode = qc_get_sql_mode();

        auto i = m_infos.find(canonical_stmt);

        if (i != m_infos.end())
        {
            Entry& entry = i->second;

            if ((entry.sql_mode == sql_mode)
                && (entry.options == this_thread.options))
            {
                mxb_assert(this_unit.classifier);
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

    void insert(std::string_view canonical_stmt, std::shared_ptr<QC_STMT_INFO> sInfo)
    {
        mxb_assert(peek(canonical_stmt) == nullptr);
        mxb_assert(this_unit.classifier);

        // 0xffffff is the maximum packet size, 4 is for packet header and 1 is for command byte. These are
        // MariaDB/MySQL protocol specific values that are also defined in <maxscale/protocol/mysql.h> but
        // should not be exposed to the core.
        constexpr int64_t max_entry_size = 0xffffff - 5;

        int64_t size = entry_size(sInfo.get());

        if (size < max_entry_size && size <= m_cache_max_size)
        {
            int64_t required_space = (m_stats.size + size) - m_cache_max_size;

            if (required_space > 0)
            {
                make_space(required_space);
            }

            if (m_stats.size + size <= m_cache_max_size)
            {
                m_infos.emplace(canonical_stmt,
                                Entry(std::move(sInfo), qc_get_sql_mode(), this_thread.options));

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

    void get_state(std::map<std::string, QC_CACHE_ENTRY>& state, size_t top) const
    {
        auto entries = limit_n(m_infos.begin(), m_infos.end(), top, [](const auto& a, const auto& b){
            return a.second.hits > b.second.hits;
        });

        for (const auto& info : entries)
        {
            std::string stmt = std::string(info.get().first);
            const Entry& entry = info.get().second;

            auto it = state.find(stmt);

            if (it == state.end())
            {
                QC_CACHE_ENTRY e {};

                e.hits = entry.hits;
                e.result = this_unit.classifier->qc_get_result_from_info(entry.sInfo.get());

                state.insert(std::make_pair(stmt, e));
            }
            else
            {
                QC_CACHE_ENTRY& e = it->second;

                e.hits += entry.hits;

                QC_STMT_RESULT result = this_unit.classifier->qc_get_result_from_info(entry.sInfo.get());

                if (result.size > e.result.size)
                {
                    // Size may differ, if data has been completely collected in one
                    // thread but not in the other. We'll return the larger value.
                    e.result.size = result.size;
                }

#if defined (SS_DEBUG)
                mxb_assert(e.result.status == result.status);
                mxb_assert(e.result.type_mask == result.type_mask);
                mxb_assert(e.result.op == result.op);
#endif
            }
        }
    }

    void evict_surplus()
    {
        if (m_cache_max_size == 0 && m_stats.size != 0)
        {
            clear();
        }
        else if (m_stats.size > m_cache_max_size)
        {
            make_space(m_stats.size - m_cache_max_size);
        }

        mxb_assert(m_stats.size <= m_cache_max_size);
    }

    int64_t clear()
    {
        int64_t size = 0;

        auto it = m_infos.begin();
        while (it != m_infos.end())
        {
            auto jt = it++;
            size += erase(jt); // Takes a & and the call will erase the iterator.
        }

        // TODO: This should be an assert, but as there seems to be a book-keeping problem,
        // TODO: so as not to break systems tests, currently we just log.

        if (m_stats.size != 0)
        {
            MXB_WARNING("After clearing all entries and %ld bytes from the cache, according "
                        "to the book-keeping there is still %ld bytes unaccounted for.",
                        size, m_stats.size);
        }

        m_stats.size = 0;

        return size;
    }

private:
    struct Entry
    {
        Entry(std::shared_ptr<QC_STMT_INFO> sInfo, qc_sql_mode_t sql_mode, uint32_t options)
            : sInfo(std::move(sInfo))
            , sql_mode(sql_mode)
            , options(options)
            , hits(0)
        {
        }

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

    int64_t erase(InfosByStmt::iterator& i)
    {
        mxb_assert(i != m_infos.end());

        int64_t size = entry_size(*i);
        m_stats.size -= size;

        mxb_assert(this_unit.classifier);
        m_infos.erase(i);

        ++m_stats.evictions;

        return size;
    }

    int64_t erase(std::string_view canonical_stmt)
    {
        int64_t size = 0;

        auto i = m_infos.find(canonical_stmt);
        mxb_assert(i != m_infos.end());

        if (i != m_infos.end())
        {
            size = erase(i);
        }

        return size;
    }

    void make_space(int64_t required_space)
    {
        int64_t freed_space = 0;

        std::uniform_int_distribution<> dis(0, m_infos.bucket_count() - 1);

        while ((freed_space < required_space) && !m_infos.empty())
        {
            freed_space += evict(dis);
        }

        mxb_assert(freed_space >= required_space);
        mxb_assert((m_infos.empty() && m_stats.size == 0) || (!m_infos.empty() && m_stats.size != 0));
    }

    int64_t evict(std::uniform_int_distribution<>& dis)
    {
        int64_t freed_space = 0;

        int start_bucket = dis(m_reng);
        int end_bucket = m_infos.bucket_count();
        mxb_assert((start_bucket >= 0) && (start_bucket < end_bucket));

        // There may be buckets that are empty. So as not to end up
        // looping "forever" while randomly looking for one that is
        // non-empty, we linearily continue towards the end if we
        // hit an empty one and continue from the beginning if we
        // still did not hit one.
        int bucket = start_bucket;
        while (freed_space == 0 && bucket < end_bucket)
        {
            auto i = m_infos.begin(bucket);

            // We just remove the first entry in the bucket. In the general case
            // there will be just one.
            if (i != m_infos.end(bucket))
            {
                freed_space += entry_size(*i);

                MXB_AT_DEBUG(int64_t size = ) erase(i->first);
                mxb_assert(size != 0);
                break;
            }

            ++bucket;

            if (bucket == end_bucket)
            {
                // Reached the end, let's continue from the beginning.
                bucket = 0;
                end_bucket = start_bucket;
            }
            else if (bucket == start_bucket)
            {
                // A full loop, but we still did not find anything to erase.
                mxb_assert(!true);
                break;
            }
        }

        return freed_space;
    }

    InfosByStmt        m_infos;
    QC_CACHE_STATS     m_stats;
    std::random_device m_rdev;
    std::mt19937       m_reng;
    int64_t            m_cache_max_size;
};

bool use_cached_result()
{
    bool rv = this_thread.use_cache;

    if (rv)
    {
        auto max_size = QCInfoCache::thread_cache_max_size();

        if (max_size != this_thread.pInfo_cache->cache_max_size())
        {
            auto* pWorker = mxs::RoutingWorker::get_current();

            mxb_assert_message(pWorker,
                               "Only routing workers can use query classification caching. "
                               "Call qc_use_local_cache(false) after qc initialization.");

            // Adjusting the cache size while the cache is being used leads to
            // various book-keeping issues. Simpler if it's done once the cache
            // is no longer being used.
            if (!this_thread.size_being_adjusted)
            {
                this_thread.size_being_adjusted = true;
                pWorker->lcall([]{
                        this_thread.pInfo_cache->update_cache_max_size();
                        this_thread.pInfo_cache->evict_surplus();
                        this_thread.size_being_adjusted = false;
                    });
            }
        }

        rv = max_size != 0;
    }

    return rv;
}

bool has_not_been_parsed(GWBUF* pStmt)
{
    // A GWBUF has not been parsed, if it does not have a parsing info object attached.
    return pStmt->get_classifier_data_ptr() == nullptr;
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
        , m_use_cached_result(use_cached_result())
        , m_info_size_before(0)
    {
        if (m_use_cached_result)
        {
            auto pInfo = static_cast<QC_STMT_INFO*>(m_pStmt->get_classifier_data_ptr());

            if (pInfo)
            {
                m_info_size_before = pInfo->size();
            }
            else
            {
                m_canonical = m_pStmt->get_canonical(); // Not from the QC, but from GWBUF.

                if (mariadb::is_com_prepare(*pStmt))
                {
                    // P as in prepare, and appended so as not to cause a
                    // need for copying the data.
                    m_canonical += ":P";
                }

                std::shared_ptr<QC_STMT_INFO> sInfo = this_thread.pInfo_cache->get(m_canonical);
                if (sInfo)
                {
                    m_info_size_before = sInfo->size();
                    m_pStmt->set_classifier_data(std::move(sInfo));
                    m_canonical.clear();    // Signals that nothing needs to be added in the destructor.
                }
            }
        }
    }

    ~QCInfoCacheScope()
    {
        if (m_use_cached_result)
        {
            bool exclude = exclude_from_cache();

            if (!m_canonical.empty() && !exclude)
            {   // Cache for the first time
                auto sInfo = m_pStmt->get_classifier_data();
                mxb_assert(sInfo);

                // Now from QC and this will have the trailing ":P" in case the GWBUF
                // contained a COM_STMT_PREPARE.
                std::string_view canonical = this_unit.classifier->qc_info_get_canonical(sInfo.get());
                mxb_assert(m_canonical == canonical);

                this_thread.pInfo_cache->insert(canonical, std::move(sInfo));
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
    }

private:
    GWBUF*      m_pStmt;
    std::string m_canonical;
    bool        m_use_cached_result;
    size_t      m_info_size_before;

    bool exclude_from_cache() const
    {
        constexpr const int is_autocommit = QUERY_TYPE_ENABLE_AUTOCOMMIT | QUERY_TYPE_DISABLE_AUTOCOMMIT;
        uint32_t type_mask = QUERY_TYPE_UNKNOWN;
        this_unit.classifier->qc_get_type_mask(m_pStmt, &type_mask);
        return (type_mask & is_autocommit) != 0;
    }
};
}

bool qc_setup(const QC_CACHE_PROPERTIES* cache_properties,
              qc_sql_mode_t sql_mode,
              const char* plugin_name,
              const char* plugin_args)
{
    QC_TRACE();
    mxb_assert(!this_unit.classifier);

    if (!plugin_name || (*plugin_name == 0))
    {
        MXB_NOTICE("No query classifier specified, using default '%s'.", DEFAULT_QC_NAME);
        plugin_name = DEFAULT_QC_NAME;
    }

    int32_t rv = QC_RESULT_ERROR;
    this_unit.classifier = qc_load(plugin_name);

    if (this_unit.classifier)
    {
        rv = this_unit.classifier->qc_setup(sql_mode, plugin_args);

        if (rv == QC_RESULT_OK)
        {
            int64_t cache_max_size = (cache_properties ? cache_properties->max_size : 0);
            mxb_assert(cache_max_size >= 0);

            if (cache_max_size)
            {
                // Config::n_threads as MaxScale is not yet running.
                int64_t size_per_thr = cache_max_size / mxs::Config::get().n_threads;
                MXB_NOTICE("Query classification results are cached and reused. "
                           "Memory used per thread: %s", mxb::pretty_size(size_per_thr).c_str());
            }
            else
            {
                MXB_NOTICE("Query classification results are not cached.");
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
             const char* plugin_name,
             const char* plugin_args)
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
            MXB_NOTICE("Transaction detection using QC.");
        }
        else if (strcmp(parse_using, "QC_TRX_PARSE_USING_PARSER") == 0)
        {
            this_unit.qc_trx_parse_using = QC_TRX_PARSE_USING_PARSER;
            MXB_NOTICE("Transaction detection using custom PARSER.");
        }
        else
        {
            MXB_NOTICE("QC_TRX_PARSE_USING set, but the value %s is not known. "
                       "Parsing using QC.",
                       parse_using);
        }
    }

    return true;
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

std::string_view qc_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    std::string_view name;

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

std::vector<QcTableName> qc_get_table_names(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    std::vector<QcTableName> names;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_table_names(query, &names);

    return names;
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

std::vector<std::string_view> qc_get_database_names(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    std::vector<std::string_view> names;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_database_names(query, &names);

    return names;
}

QC_KILL qc_get_kill_info(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    QC_KILL rval;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_kill_info(query, &rval);

    return rval;
}

std::string_view qc_get_prepare_name(GWBUF* query)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    std::string_view name;

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

const char* qc_kill_type_to_string(qc_kill_type_t type)
{
    switch (type)
    {
    case QC_KILL_CONNECTION:
        return "QC_KILL_CONNECTION";

    case QC_KILL_QUERY:
        return "QC_KILL_QUERY";

    case QC_KILL_QUERY_ID:
        return "QC_KILL_QUERY_ID";

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

    case QUERY_OP_SET:
        return "QUERY_OP_SET";

    case QUERY_OP_SET_TRANSACTION:
        return "QUERY_OP_SET_TRANSACTION";

    case QUERY_OP_SHOW:
        return "QUERY_OP_SHOW";

    case QUERY_OP_TRUNCATE:
        return "QUERY_OP_TRUNCATE";

    case QUERY_OP_UPDATE:
        return "QUERY_OP_UPDATE";

    case QUERY_OP_KILL:
        return "QUERY_OP_KILL";

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

    case QUERY_TYPE_READONLY:
        {
            static const char name[] = "QUERY_TYPE_READONLY";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READWRITE:
        {
            static const char name[] = "QUERY_TYPE_READWRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_NEXT_TRX:
        {
            static const char name[] = "QUERY_TYPE_NEXT_TRX";
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
    QUERY_TYPE_READONLY,
    QUERY_TYPE_READWRITE,
    QUERY_TYPE_NEXT_TRX,
};

static const int N_QUERY_TYPES = sizeof(QUERY_TYPES) / sizeof(QUERY_TYPES[0]);
static const int QUERY_TYPE_MAX_LEN = 29;   // strlen("QUERY_TYPE_PREPARE_NAMED_STMT");

std::string qc_typemask_to_string(uint32_t types)
{
    std::string rv;

    for (int i = 0; i < N_QUERY_TYPES; ++i)
    {
        qc_query_type_t type = QUERY_TYPES[i];

        if (types & type)
        {
            if (!rv.empty())
            {
                rv += "|";
            }

            struct type_name_info info = type_to_type_name_info(type);

            rv += info.name;
        }
    }

    return rv;
}

uint32_t qc_remove_non_trx_type_bits(uint32_t type_mask)
{
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
                      | QUERY_TYPE_DISABLE_AUTOCOMMIT
                      | QUERY_TYPE_READONLY
                      | QUERY_TYPE_READWRITE
                      | QUERY_TYPE_NEXT_TRX);
    }

    return type_mask;
}

static uint32_t qc_get_trx_type_mask_using_qc(GWBUF* stmt)
{
    uint32_t type_mask = qc_get_type_mask(stmt);

    return qc_remove_non_trx_type_bits(type_mask);
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

    qc_sql_mode_t sql_mode = QC_SQL_MODE_DEFAULT;
    int32_t rv = this_unit.classifier->qc_get_sql_mode(&sql_mode);
    mxb_assert(rv == QC_RESULT_OK);

    return sql_mode;
}

void qc_set_sql_mode(qc_sql_mode_t sql_mode)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    int32_t rv = this_unit.classifier->qc_set_sql_mode(sql_mode);
    mxb_assert(rv == QC_RESULT_OK);
}

uint32_t qc_get_options()
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    return this_unit.classifier->qc_get_options();
}

bool qc_set_options(uint32_t options)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    int32_t rv = this_unit.classifier->qc_set_options(options);

    if (rv == QC_RESULT_OK)
    {
        this_thread.options = options;
    }

    return rv == QC_RESULT_OK;
}

bool qc_get_current_stmt(const char** ppStmt, size_t* pLen)
{
    QC_TRACE();
    mxb_assert(this_unit.classifier);

    return this_unit.classifier->qc_get_current_stmt(ppStmt, pLen) == QC_RESULT_OK;
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
        if (properties->max_size == 0)
        {
            MXB_NOTICE("Query classifier cache disabled.");
        }

        this_unit.set_cache_max_size(properties->max_size);
        rv = true;
    }
    else
    {
        MXB_ERROR("Ignoring attempt to set size of query classifier "
                  "cache to a negative value: %" PRIi64 ".",
                  properties->max_size);
    }

    return rv;
}

void qc_use_local_cache(bool enabled)
{
    this_thread.use_cache = enabled;

    if (!enabled)
    {
        if (this_thread.pInfo_cache)
        {
            this_thread.pInfo_cache->clear();
        }
    }
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
    json_t* pParams = mxb::json_ptr(pJson, MXS_JSON_PTR_PARAMETERS);

    if (pParams && json_is_object(pParams))
    {
        if (auto pSize = mxb::json_ptr(pParams, CN_CACHE_SIZE))
        {
            if (!json_is_null(pSize) && !json_is_integer(pSize))
            {
                pParams = nullptr;
            }
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

        if ((pValue = mxb::json_ptr(pParams, CN_CACHE_SIZE)))
        {
            cache_properties.max_size = json_integer_value(pValue);
            // If get_params() did its job, then we will not
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

                      if (!info.database.empty())
                      {
                          name += info.database;
                          name += '.';
                          mxb_assert(!info.table.empty());
                      }

                      if (!info.table.empty())
                      {
                          name += info.table;
                          name += '.';
                      }

                      mxb_assert(!info.column.empty());

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

                      json_object_set_new(pFunction, CN_NAME, json_stringn(info.name.data(), info.name.length()));

                      append_field_info(pFunction, CN_ARGUMENTS, info.fields, info.fields + info.n_fields);

                      json_array_append_new(pFunctions, pFunction);
                  });

    json_object_set_new(pParams, CN_FUNCTIONS, pFunctions);
}
}

std::unique_ptr<json_t> qc_classify_as_json(const char* zHost, const std::string& statement)
{
    json_t* pAttributes = json_object();

    std::unique_ptr<GWBUF> sBuffer(modutil_create_query(statement.c_str()));
    GWBUF* pBuffer = sBuffer.get();

    qc_parse_result_t result = qc_parse(pBuffer, QC_COLLECT_ALL);

    json_object_set_new(pAttributes, CN_PARSE_RESULT, json_string(qc_result_to_string(result)));

    if (result != QC_QUERY_INVALID)
    {
        std::string type_mask = qc_typemask_to_string(qc_get_type_mask(pBuffer));
        json_object_set_new(pAttributes, CN_TYPE_MASK, json_string(type_mask.c_str()));

        json_object_set_new(pAttributes, CN_OPERATION,
                            json_string(qc_op_to_string(qc_get_operation(pBuffer))));

        append_field_info(pAttributes, pBuffer);
        append_function_info(pAttributes, pBuffer);

        json_object_set_new(pAttributes, CN_CANONICAL, json_string(pBuffer->get_canonical().c_str()));
    }

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_CLASSIFY, pSelf));
}

namespace
{

json_t* cache_entry_as_json(const std::string& stmt, const QC_CACHE_ENTRY& entry)
{
    json_t* pHits = json_integer(entry.hits);

    json_t* pClassification = json_object();
    json_object_set_new(pClassification,
                        CN_PARSE_RESULT, json_string(qc_result_to_string(entry.result.status)));
    std::string type_mask = qc_typemask_to_string(entry.result.type_mask);
    json_object_set_new(pClassification, CN_TYPE_MASK, json_string(type_mask.c_str()));
    json_object_set_new(pClassification,
                        CN_OPERATION,
                        json_string(qc_op_to_string(entry.result.op)));
    json_object_set_new(pClassification,
                        CN_SIZE, json_integer(entry.result.size));

    json_t* pAttributes = json_object();
    json_object_set_new(pAttributes, CN_HITS, pHits);
    json_object_set_new(pAttributes, CN_CLASSIFICATION, pClassification);

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(stmt.c_str()));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_CACHE));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return pSelf;
}
}

std::unique_ptr<json_t> qc_cache_as_json(const char* zHost, size_t top)
{
    std::map<std::string, QC_CACHE_ENTRY> state;

    // Assuming the classification cache of all workers will roughly be similar
    // (which will be the case unless something is broken), collecting the
    // information serially from all routing workers will consume 1/N of the
    // memory that would be consumed if the information were collected in
    // parallel and then coalesced here.

    mxs::RoutingWorker::execute_serially([&]() {
        qc_get_cache_state(state, top);
    });

    auto entries = limit_n(state.begin(), state.end(), top, [](const auto& a, const auto& b){
        return a.second.hits > b.second.hits;
    });

    json_t* pData = json_array();

    for (const auto& p : entries)
    {
        const auto& stmt = p.get().first;
        const auto& entry = p.get().second;

        json_t* pEntry = cache_entry_as_json(stmt, entry);

        json_array_append_new(pData, pEntry);
    }

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_CACHE, pData));
}

void qc_get_cache_state(std::map<std::string, QC_CACHE_ENTRY>& state, size_t top)
{
    QCInfoCache* pCache = this_thread.pInfo_cache;

    if (pCache)
    {
        pCache->get_state(state, top);
    }
}

int64_t qc_clear_thread_cache()
{
    int64_t rv = 0;
    QCInfoCache* pCache = this_thread.pInfo_cache;

    if (pCache)
    {
        rv = pCache->clear();
    }

    return rv;
}
