/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <map>
#include <maxscale/parser.hh>

namespace maxscale
{

class CachingParser : public mxs::Parser
{
public:
    CachingParser(const CachingParser&) = delete;
    CachingParser& operator = (const CachingParser&) = delete;

    /**
     * Properties specifies the limits of the parser cache.
     */
    struct Properties
    {
        int64_t max_size { 0 };   /** The maximum size of the cache. */
    };

    /**
     * Public interface to parser cache state.
     */
    struct Entry
    {
        int64_t    hits { 0 };
        StmtResult result;
    };

    /**
     * Stats provides statistics of the cache.
     */
    struct Stats
    {
        int64_t size { 0 };      /** The current size of the cache. */
        int64_t inserts { 0 };   /** The number of inserts. */
        int64_t hits { 0 };      /** The number of hits. */
        int64_t misses { 0 };    /** The number of misses. */
        int64_t evictions { 0 }; /** The number of evictions. */
    };

    static void thread_init();
    static void thread_finish();

    static bool                    set_properties(const Properties& properties);
    static void                    get_properties(Properties* pProperties);

    static bool                    set_properties(json_t* pJson);
    static std::unique_ptr<json_t> get_properties_as_resource(const char* zHost);

    static int64_t                 clear_thread_cache();
    static void                    get_thread_cache_state(std::map<std::string, Entry>& state, int top);
    static bool                    get_thread_cache_stats(Stats* pStats);
    static std::unique_ptr<json_t> get_thread_cache_stats_as_json();
    static void                    set_thread_cache_enabled(bool enable);

    static std::unique_ptr<json_t> content_as_resource(const char* zHost, int top);


    Result           parse(const GWBUF& stmt, uint32_t collect) const override;

    std::string_view get_canonical(const GWBUF& stmt) const override;
    DatabaseNames    get_database_names(const GWBUF& stmt) const override;
    void             get_field_info(const GWBUF& stmt,
                                    const FieldInfo** ppInfos,
                                    size_t* pnInfos) const override;
    void             get_function_info(const GWBUF& stmt,
                                       const FunctionInfo** infos,
                                       size_t* n_infos) const override;
    KillInfo         get_kill_info(const GWBUF& stmt) const override;
    sql::OpCode      get_operation(const GWBUF& stmt) const override;
    uint32_t         get_options() const override;
    GWBUF*           get_preparable_stmt(const GWBUF& stmt) const override;
    std::string_view get_prepare_name(const GWBUF& stmt) const override;
    uint64_t         get_server_version() const override;
    SqlMode          get_sql_mode() const override;
    TableNames       get_table_names(const GWBUF& stmt) const override;
    uint32_t         get_trx_type_mask(const GWBUF& stmt) const override;
    uint32_t         get_type_mask(const GWBUF& stmt) const override;
    bool             relates_to_previous(const GWBUF& stmt) const override;
    bool             is_multi_stmt(const GWBUF& stmt) const override;

    bool set_options(uint32_t options) override;
    void set_sql_mode(SqlMode sql_mode) override;
    void set_server_version(uint64_t version) override;

    QueryInfo get_query_info(const GWBUF& stmt) const override;

protected:
    CachingParser(std::unique_ptr<Parser> sParser);

    std::unique_ptr<Parser> m_sParser;
};

}
