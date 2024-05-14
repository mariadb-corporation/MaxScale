/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cachingparser.hh>
#include <atomic>
#include <map>
#include <random>
#include <maxbase/checksum.hh>
#include <maxsimd/canonical.hh>
#include <maxscale/buffer.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/routingworker.hh>

namespace
{

using mxs::CachingParser;

const char CN_CACHE[] = "cache";
const char CN_CACHE_SIZE[] = "cache_size";
const char CN_CLASSIFICATION[] = "classification";
const char CN_HITS[] = "hits";

class ThisUnit
{
public:
    ThisUnit()
        : m_cache_max_size(std::numeric_limits<int64_t>::max())
    {
    }

    ThisUnit(const ThisUnit&) = delete;
    ThisUnit& operator=(const ThisUnit&) = delete;

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
    QCInfoCache* pInfo_cache {nullptr};
    uint32_t     options {0};
    bool         use_cache {true};
    bool         size_being_adjusted { false };
    std::string  canonical;
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
 * the GWBUF::ProtocolInfo object created by the actual query classifier.
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
    }

    ~QCInfoCache()
    {
    }

    void inc_ref()
    {
        mxb_assert(m_refs >= 0);
        ++m_refs;
    }

    int32_t dec_ref()
    {
        mxb_assert(m_refs > 0);
        return --m_refs;
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

    GWBUF::ProtocolInfo* peek(std::string_view canonical_stmt) const
    {
        auto i = m_infos.find(canonical_stmt);

        return i != m_infos.end() ? i->second.sInfo.get() : nullptr;
    }

    std::shared_ptr<GWBUF::ProtocolInfo> get(mxs::Parser* pParser, std::string_view canonical_stmt)
    {
        std::shared_ptr<GWBUF::ProtocolInfo> sInfo;
        mxs::Parser::SqlMode sql_mode = pParser->get_sql_mode();

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

    void insert(mxs::Parser* pParser,
                std::string_view canonical_stmt,
                std::shared_ptr<GWBUF::ProtocolInfo> sInfo)
    {
        mxb_assert(peek(canonical_stmt) == nullptr);

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
                mxs::Parser::SqlMode sql_mode = pParser->get_sql_mode();

                m_infos.emplace(canonical_stmt,
                                Entry(pParser, std::move(sInfo), sql_mode, this_thread.options));

                ++m_stats.inserts;
                m_stats.size += size;
            }
        }
    }

    void update_total_size(int32_t delta)
    {
        m_stats.size += delta;
    }

    void get_stats(CachingParser::Stats* pStats)
    {
        *pStats = m_stats;
    }

    void get_state(std::map<std::string, CachingParser::Entry>& state, int top) const
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
                CachingParser::Entry e {};

                e.hits = entry.hits;
                e.result = entry.pParser->plugin().get_stmt_result(entry.sInfo.get());

                state.insert(std::make_pair(stmt, e));
            }
            else
            {
                CachingParser::Entry& e = it->second;

                e.hits += entry.hits;

                auto& plugin = entry.pParser->plugin();
                mxs::Parser::StmtResult result = plugin.get_stmt_result(entry.sInfo.get());

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
        Entry(mxs::Parser* pParser,
              std::shared_ptr<GWBUF::ProtocolInfo> sInfo,
              mxs::Parser::SqlMode sql_mode,
              uint32_t options)
            : pParser(pParser)
            , sInfo(std::move(sInfo))
            , sql_mode(sql_mode)
            , options(options)
            , hits(0)
        {
        }

        mxs::Parser*                         pParser;
        std::shared_ptr<GWBUF::ProtocolInfo> sInfo;
        mxs::Parser::SqlMode                 sql_mode;
        uint32_t                             options;
        int64_t                              hits;
    };

    typedef std::unordered_map<std::string_view, Entry, mxb::xxHasher> InfosByStmt;

    int64_t entry_size(const GWBUF::ProtocolInfo* pInfo)
    {
        const int64_t map_entry_overhead = 4 * sizeof(void*);
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

    InfosByStmt          m_infos;
    CachingParser::Stats m_stats;
    std::random_device   m_rdev;
    std::mt19937         m_reng;
    int64_t              m_cache_max_size;
    int32_t              m_refs {0};
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

bool has_not_been_parsed(const GWBUF& stmt)
{
    // A GWBUF has not been parsed, if it does not have a protocol info object attached.
    return stmt.get_protocol_info().get() == nullptr;
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

    /**
     * Construct a new QCInfoCacheScope
     *
     * If `pStmt` hasn't been parsed and a cached entry is found, a reference to the parsing info is attached
     * to the GWBUF in the constructor. If no cached entry is found, the query is parsed and the destructor
     * inserts the parsing info into the cache.
     *
     * @param pParser The parser to use. Must be non-null and remain valid for the lifetime of this object.
     * @param pStmt   The query to parse. Must be non-null and remain valid for the lifetime of this object.
     */
    QCInfoCacheScope(mxs::Parser* pParser, const GWBUF* pStmt)
        : m_parser(*pParser)
        , m_stmt(*pStmt)
        , m_use_cached_result(use_cached_result())
        , m_info_size_before(0)
    {
        if (m_use_cached_result)
        {
            if (const auto& sCached = m_stmt.get_protocol_info())
            {
                // The buffer already has the info. This means this is not the first time that a query
                // classification function is called. Record the current size of the value so that we'll
                // be able to detect in the destructor if it has grown.
                m_info_size_before = sCached->size();
            }
            else
            {
                // We generate the canonical explicitly, because now we want the key that
                // allows us to look up whether the parsing info already exists. Besides,
                // calling m_parser.get_canonical(m_stmt) would cause an infinite recursion.
                this_thread.canonical = m_parser.get_sql(m_stmt);
                maxsimd::get_canonical(&this_thread.canonical);

                if (m_parser.is_prepare(m_stmt))
                {
                    // P as in prepare, and appended so as not to cause a
                    // need for copying the data.
                    this_thread.canonical += ":P";
                }

                std::shared_ptr<GWBUF::ProtocolInfo> sInfo =
                    this_thread.pInfo_cache->get(&m_parser,  this_thread.canonical);
                if (sInfo)
                {
                    // Cache hit, copy the reference into the GWBUF
                    m_info_size_before = sInfo->size();
                    const_cast<GWBUF&>(m_stmt).set_protocol_info(std::move(sInfo));
                }
                else if (!this_thread.canonical.empty())
                {
                    // Cache miss, try to insert it into the cache in the destructor
                    m_info_size_before = ADD_TO_CACHE;
                }
            }
        }
    }

    ~QCInfoCacheScope()
    {
        if (m_use_cached_result)
        {
            const auto& sInfo = m_stmt.get_protocol_info();

            if (sInfo && sInfo->cacheable())
            {
                if (m_info_size_before == ADD_TO_CACHE)
                {
                    // Now from QC and this will have the trailing ":P" in case the GWBUF
                    // contained a COM_STMT_PREPARE.
                    std::string_view canonical = m_parser.plugin().get_canonical(sInfo.get());
                    mxb_assert(this_thread.canonical == canonical);

                    this_thread.pInfo_cache->insert(&m_parser, canonical, sInfo);
                }
                else if (auto info_size_after = sInfo->size(); m_info_size_before != info_size_after)
                {
                    // The size has changed
                    mxb_assert(m_info_size_before < info_size_after);
                    this_thread.pInfo_cache->update_total_size(info_size_after - m_info_size_before);
                }
            }
        }
    }

private:
    // The constant that's stored in m_info_size_before when the entry should be inserted into the cache.
    static constexpr size_t ADD_TO_CACHE = std::numeric_limits<size_t>::max();

    mxs::Parser& m_parser;
    const GWBUF& m_stmt;
    bool         m_use_cached_result;
    size_t       m_info_size_before;
};
}

namespace maxscale
{

CachingParser::CachingParser(std::unique_ptr<Parser> sParser)
    : Parser(&sParser->plugin(), &sParser->helper())
    , m_sParser(std::move(sParser))
{
}

// static
void CachingParser::thread_init()
{
    if (!this_thread.pInfo_cache)
    {
        this_thread.pInfo_cache = new QCInfoCache;
    }

    this_thread.pInfo_cache->inc_ref();
}

// static
void CachingParser::thread_finish()
{
    mxb_assert(this_thread.pInfo_cache);

    if (this_thread.pInfo_cache->dec_ref() == 0)
    {
        delete this_thread.pInfo_cache;
        this_thread.pInfo_cache = nullptr;
    }
}

// static
bool CachingParser::set_properties(const Properties& properties)
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
                  "cache to a negative value: %lu.", properties.max_size);
    }

    return rv;
}

// static
void CachingParser::get_properties(Properties* pProperties)
{
    pProperties->max_size = this_unit.cache_max_size();
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

// static
bool CachingParser::set_properties(json_t* pJson)
{
    bool rv = false;

    json_t* pParams = get_params(pJson);

    if (pParams)
    {
        rv = true;

        Properties cache_properties;
        get_properties(&cache_properties);

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
            MXB_AT_DEBUG(bool set = ) CachingParser::set_properties(cache_properties);
            mxb_assert(set);
        }
    }

    return rv;
}

// static
std::unique_ptr<json_t> CachingParser::get_properties_as_resource(const char* zHost)
{
    Properties properties;
    get_properties(&properties);

    json_t* pParams = json_object();
    json_object_set_new(pParams, CN_CACHE_SIZE, json_integer(properties.max_size));

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

json_t* cache_entry_as_json(const std::string& stmt, const CachingParser::Entry& entry)
{
    json_t* pHits = json_integer(entry.hits);

    json_t* pClassification = json_object();
    json_object_set_new(pClassification,
                        CN_PARSE_RESULT, json_string(mxs::parser::to_string(entry.result.status)));
    std::string type_mask = mxs::Parser::type_mask_to_string(entry.result.type_mask);
    json_object_set_new(pClassification, CN_TYPE_MASK, json_string(type_mask.c_str()));
    json_object_set_new(pClassification,
                        CN_OPERATION,
                        json_string(mxs::sql::to_string(entry.result.op)));
    json_object_set_new(pClassification, CN_SIZE, json_integer(entry.result.size));

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

std::unique_ptr<json_t> CachingParser::content_as_resource(const char* zHost, int top)
{
    std::map<std::string, Entry> state;

    // Assuming the classification cache of all workers will roughly be similar
    // (which will be the case unless something is broken), collecting the
    // information serially from all routing workers will consume 1/N of the
    // memory that would be consumed if the information were collected in
    // parallel and then coalesced here.

    mxs::RoutingWorker::execute_serially([&]() {
        CachingParser::get_thread_cache_state(state, top);
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

// static
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

// static
void CachingParser::get_thread_cache_state(std::map<std::string, Entry>& state, int top)
{
    QCInfoCache* pCache = this_thread.pInfo_cache;

    if (pCache)
    {
        pCache->get_state(state, top);
    }
}

// static
bool CachingParser::get_thread_cache_stats(Stats* pStats)
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

// static
std::unique_ptr<json_t> CachingParser::get_thread_cache_stats_as_json()
{
    Stats stats;
    get_thread_cache_stats(&stats);

    std::unique_ptr<json_t> sStats {json_object()};
    json_object_set_new(sStats.get(), "size", json_integer(stats.size));
    json_object_set_new(sStats.get(), "inserts", json_integer(stats.inserts));
    json_object_set_new(sStats.get(), "hits", json_integer(stats.hits));
    json_object_set_new(sStats.get(), "misses", json_integer(stats.misses));
    json_object_set_new(sStats.get(), "evictions", json_integer(stats.evictions));

    return sStats;
}

// static
void CachingParser::set_thread_cache_enabled(bool enabled)
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

Parser::Result CachingParser::parse(const GWBUF& stmt, uint32_t collect) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);

    return m_sParser->parse(stmt, collect);
}

std::string_view CachingParser::get_canonical(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_canonical(stmt);
}

CachingParser::DatabaseNames CachingParser::get_database_names(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_database_names(stmt);
}

void CachingParser::get_field_info(const GWBUF& stmt,
                                   const FieldInfo** ppInfos,
                                   size_t* pnInfos) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    m_sParser->get_field_info(stmt, ppInfos, pnInfos);
}

void CachingParser::get_function_info(const GWBUF& stmt,
                                      const FunctionInfo** ppInfos,
                                      size_t* pnInfos) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    m_sParser->get_function_info(stmt, ppInfos, pnInfos);
}

Parser::KillInfo CachingParser::get_kill_info(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_kill_info(stmt);
}

sql::OpCode CachingParser::get_operation(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_operation(stmt);
}

uint32_t CachingParser::get_options() const
{
    return m_sParser->get_options();
}

GWBUF* CachingParser::get_preparable_stmt(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_preparable_stmt(stmt);
}

std::string_view CachingParser::get_prepare_name(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_prepare_name(stmt);
}

uint64_t CachingParser::get_server_version() const
{
    return m_sParser->get_server_version();
}

Parser::SqlMode CachingParser::get_sql_mode() const
{
    return m_sParser->get_sql_mode();
}

CachingParser::TableNames CachingParser::get_table_names(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_table_names(stmt);
}

uint32_t CachingParser::get_trx_type_mask(const GWBUF& stmt) const
{
    return m_sParser->get_trx_type_mask(stmt);
}

uint32_t CachingParser::get_type_mask(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_type_mask(stmt);
}

bool CachingParser::relates_to_previous(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->relates_to_previous(stmt);
}

bool CachingParser::is_multi_stmt(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->is_multi_stmt(stmt);
}

bool CachingParser::set_options(uint32_t options)
{
    bool rv = m_sParser->set_options(options);

    if (rv)
    {
        this_thread.options = options;
    }

    return rv;
}

void CachingParser::set_sql_mode(Parser::SqlMode sql_mode)
{
    m_sParser->set_sql_mode(sql_mode);
}

void CachingParser::set_server_version(uint64_t version)
{
    m_sParser->set_server_version(version);
}

mxs::Parser::QueryInfo CachingParser::get_query_info(const GWBUF& stmt) const
{
    QCInfoCacheScope scope(m_sParser.get(), &stmt);
    return m_sParser->get_query_info(stmt);
}
}
