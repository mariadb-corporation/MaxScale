/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "qc_sqlite"
#include <sqliteInt.h>

#include <signal.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <new>
#include <string>
#include <vector>
#include <mutex>

#include <maxbase/alloc.hh>
#include <maxbase/string.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/trxboundaryparser.hh>
#include <maxscale/utils.hh>

#include "builtin_functions.hh"

using std::vector;
using std::string_view;
using std::string;
using mxb::sv_case_eq;
using mxs::Parser;
using mxs::ParserPlugin;

// #define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined (QC_TRACE_ENABLED)
#define QC_TRACE() MXB_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

#define QC_EXCEPTION_GUARD(statement) \
    do {try {statement;} \
        catch (const std::bad_alloc&) { \
            MXB_OOM(); pInfo->m_status = Parser::Result::INVALID;} \
        catch (const std::exception& x) { \
            MXB_ERROR("Caught standard exception: %s", x.what()); pInfo->m_status = Parser::Result::INVALID;} \
        catch (...) { \
            MXB_ERROR("Caught unknown exception."); pInfo->m_status = Parser::Result::INVALID;}} while (false)

static inline bool qc_info_was_tokenized(mxs::Parser::Result status)
{
    return status == Parser::Result::TOKENIZED;
}

static inline bool qc_info_was_parsed(mxs::Parser::Result status)
{
    return status == Parser::Result::PARSED;
}

typedef enum qc_log_level
{
    QC_LOG_NOTHING = 0,
    QC_LOG_NON_PARSED,
    QC_LOG_NON_PARTIALLY_PARSED,
    QC_LOG_NON_TOKENIZED,
} qc_log_level_t;

/**
 * Defines what a particular name should be mapped to.
 */
typedef struct qc_name_mapping
{
    const char* from;
    const char* to;
} QC_NAME_MAPPING;

static QC_NAME_MAPPING function_name_mappings_default[] =
{
    // NOTE: If something is added here, add it to function_name_mappings_oracle as well.
    {"now", "current_timestamp"},
    {NULL,  NULL               }
};

static QC_NAME_MAPPING function_name_mappings_oracle[] =
{
    {"now", "current_timestamp"},
    {"nvl", "ifnull"           },
    {NULL,  NULL               }
};

/**
 * Stores alias information. The key in the mapping is the alias name,
 * and an instance of this struct contains the actual table/database.
 */

struct QcAliasValue
{
    QcAliasValue() = default;

    QcAliasValue(string d, string t)
        : database(d)
        , table(t)
    {
    }

    string database;
    string table;
};

using QcAliases = std::map<string, QcAliasValue>;

/**
 * The state of qc_sqlite.
 */
static struct
{
    bool             initialized;
    bool             setup;
    qc_log_level_t   log_level;
    Parser::SqlMode  sql_mode;
    QC_NAME_MAPPING* pFunction_name_mappings;
    std::mutex       lock;
} this_unit;

/**
 * The qc_sqlite thread-specific state.
 */
class QcSqliteInfo;

static thread_local struct
{
    bool                     initialized;   // Whether the thread specific data has been initialized.
    sqlite3*                 pDb;           // Thread specific database handle.
    Parser::SqlMode          sql_mode;      // What sql_mode is used.
    uint32_t                 options;       // Options affecting classification.
    QcSqliteInfo*            pInfo;         // The information for the current statement being classified.
    uint64_t                 version;       // Encoded version number
    uint32_t                 version_major;
    uint32_t                 version_minor;
    uint32_t                 version_patch;
    QC_NAME_MAPPING*         pFunction_name_mappings;   // How function names should be mapped.
    const Parser::Helper*    pHelper;
    std::vector<const char*> markers;       // For use with maxsimd::get_canonical()
} this_thread;

const uint32_t VERSION_MAJOR_DEFAULT = 10;
const uint32_t VERSION_MINOR_DEFAULT = 3;
const uint32_t VERSION_PATCH_DEFAULT = 0;
const uint64_t VERSION_DEFAULT = VERSION_MAJOR_DEFAULT * 10000
    + VERSION_MINOR_DEFAULT * 100
    + VERSION_PATCH_DEFAULT;

/**
 * HELPERS
 */

typedef enum qc_token_position
{
    QC_TOKEN_MIDDLE,    // In the middle or irrelevant, e.g.: "=" in "a = b".
    QC_TOKEN_LEFT,      // To the left, e.g.: "a" in "a = b".
    QC_TOKEN_RIGHT,     // To the right, e.g: "b" in "a = b".
} qc_token_position_t;

static bool        ensure_query_is_parsed(const Parser::Helper& helper, const GWBUF* query, uint32_t collect);
static const char* map_function_name(QC_NAME_MAPPING* function_name_mappings, const char* name);
static bool        parse_query(const Parser::Helper& helper, const GWBUF& query, uint32_t collect);
static void        parse_query_string(const char* query, int len, bool suppress_logging);
static bool        query_is_parsed(const GWBUF* query, uint32_t collect);
static bool        should_exclude(const char* zName, const ExprList* pExclude);
static const char* get_token_symbol(int token);

// Defined in parse.y
extern "C"
{

extern void exposed_sqlite3ExprDelete(sqlite3* db, Expr* pExpr);
extern void exposed_sqlite3ExprListDelete(sqlite3* db, ExprList* pList);
extern void exposed_sqlite3IdListDelete(sqlite3* db, IdList* pList);
extern void exposed_sqlite3SrcListDelete(sqlite3* db, SrcList* pList);
extern void exposed_sqlite3SelectDelete(sqlite3* db, Select* p);

extern void exposed_sqlite3BeginTrigger(Parse* pParse,
                                        Token* pName1,
                                        Token* pName2,
                                        int tr_tm,
                                        int op,
                                        IdList* pColumns,
                                        SrcList* pTableName,
                                        Expr* pWhen,
                                        int isTemp,
                                        int noErr);
extern void exposed_sqlite3FinishTrigger(Parse* pParse,
                                         TriggerStep* pStepList,
                                         Token* pAll);
extern int  exposed_sqlite3Dequote(char* z);
extern int  exposed_sqlite3EndTable(Parse*, Token*, Token*, u8, Select*);
extern void exposed_sqlite3Insert(Parse* pParse,
                                  SrcList* pTabList,
                                  Select* pSelect,
                                  IdList* pColumns,
                                  int onError);
extern int  exposed_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest);
extern void exposed_sqlite3StartTable(Parse* pParse,    /* Parser context */
                                      Token* pName1,    /* First part of the name of the table or view */
                                      Token* pName2,    /* Second part of the name of the table or view */
                                      int isTemp,       /* True if this is a TEMP table */
                                      int isView,       /* True if this is a VIEW */
                                      int isVirtual,    /* True if this is a VIRTUAL table */
                                      int noErr);       /* Do nothing if table already exists */
extern void exposed_sqlite3Update(Parse* pParse,
                                  SrcList* pTabList,
                                  ExprList* pChanges,
                                  Expr* pWhere,
                                  int onError);
}

/**
 * Contains information about a particular query.
 */
class QcSqliteInfo : public GWBUF::ProtocolInfo
{
    QcSqliteInfo(const QcSqliteInfo&);
    QcSqliteInfo& operator=(const QcSqliteInfo&);

public:

    void calculate_size()
    {
        using std::for_each;

        int32_t size = sizeof(*this);

        if (m_pPreparable_stmt)
        {
            size += sizeof(*m_pPreparable_stmt);
            size += gwbuf_length(m_pPreparable_stmt);
        }

        // m_canonical not to be shrink_to_fit(). Not needed and should actually be
        // shrunk, all string_views would be invalidated.
        size += m_canonical.size();

        m_table_names.shrink_to_fit();
        size += m_table_names.capacity() * sizeof(Parser::TableName);

        m_field_infos.shrink_to_fit();
        size += m_field_infos.capacity() * sizeof(Parser::FieldInfo);

        using VQFI = vector<Parser::FieldInfo>;
        m_function_field_usage.shrink_to_fit();
        size += m_function_field_usage.capacity() * sizeof(VQFI);
        for_each(m_function_field_usage.begin(), m_function_field_usage.end(), [&size](VQFI& v) {
                v.shrink_to_fit();
                size += v.capacity() * sizeof(Parser::FieldInfo);
            });

        m_function_infos.shrink_to_fit();
        size += m_function_infos.capacity() * sizeof(Parser::FunctionInfo);
        // Since the function infos point into function field usages, we must
        // now ensure that, in case m_function_field_usage really was shrank
        // to fit, that we do not point into la-la land.
        int i = 0;
        for_each(m_function_infos.begin(), m_function_infos.end(), [this, &i](Parser::FunctionInfo& info) {
                VQFI& v = m_function_field_usage[i];

                info.fields = v.data();
                info.n_fields = v.size();
                ++i;
            });

        using VC = vector<char>;
        m_scratch_buffers.shrink_to_fit();
        size += m_scratch_buffers.capacity() * sizeof(VC);
        for_each(m_scratch_buffers.begin(), m_scratch_buffers.end(), [&size](VC& v) {
                // 'v', the scratch buffer, cannot be shrank to fit as fields point to it.
                // It should be of exactly the right size anyway.
                size += v.capacity() * sizeof(char);
            });

        m_size = size;
    }

    size_t size() const override
    {
        return m_size;
    }

    Parser::StmtResult get_result() const
    {
        Parser::StmtResult result =
        {
            m_status,
            m_type_mask,
            m_operation
        };

        return result;
    }

    static std::unique_ptr<QcSqliteInfo> create(uint32_t collect)
    {
        return std::make_unique<QcSqliteInfo>(collect);
    }

    static QcSqliteInfo* get(const mxs::Parser::Helper& helper, const GWBUF* pStmt, uint32_t collect)
    {
        QcSqliteInfo* pInfo = nullptr;

        if (ensure_query_is_parsed(helper, pStmt, collect))
        {
            pInfo = static_cast<QcSqliteInfo*>(pStmt->get_protocol_info().get());
            mxb_assert(pInfo);
        }

        return pInfo;
    }

    bool is_valid() const
    {
        return m_status != Parser::Result::INVALID;
    }

    std::string_view get_canonical() const
    {
        return m_canonical;
    }

    bool get_type_mask(uint32_t* pType_mask) const
    {
        bool rv = false;

        if (is_valid())
        {
            *pType_mask = m_type_mask;
            rv = true;
        }

        return rv;
    }

    bool get_operation(mxs::sql::OpCode* pOp) const
    {
        bool rv = false;

        if (is_valid())
        {
            *pOp = m_operation;
            rv = true;
        }

        return rv;
    }

    bool get_created_table_name(string_view* pCreated_table_name) const
    {
        bool rv = false;

        if (is_valid())
        {
            if (!m_created_table_name.empty())
            {
                *pCreated_table_name = m_created_table_name;
            }
            rv = true;
        }

        return rv;
    }

    bool get_table_names(vector<Parser::TableName>* pTables) const
    {
        bool rv = false;

        if (is_valid())
        {
            pTables->assign(m_table_names.begin(), m_table_names.end());

            rv = true;
        }

        return rv;
    }

    bool get_database_names(vector<string_view>* pNames) const
    {
        bool rv = false;

        if (is_valid())
        {
            pNames->assign(m_database_names.begin(), m_database_names.end());
            rv = true;
        }

        return rv;
    }

    bool get_kill_info(Parser::KillInfo* pKill) const
    {
        bool rv = false;

        if (is_valid())
        {
            *pKill = m_kill;
            rv = true;
        }

        return rv;
    }

    bool get_prepare_name(string_view* pPrepare_name) const
    {
        bool rv = false;

        if (is_valid())
        {
            *pPrepare_name = m_prepare_name;

            rv = true;
        }

        return rv;
    }

    bool get_field_info(const Parser::FieldInfo** ppInfos, size_t* pnInfos) const
    {
        bool rv = false;

        if (is_valid())
        {
            *ppInfos = m_field_infos.size() ? &m_field_infos[0] : NULL;
            *pnInfos = m_field_infos.size();

            rv = true;
        }

        return rv;
    }

    bool get_function_info(const Parser::FunctionInfo** ppInfos, size_t* pnInfos) const
    {
        bool rv = false;

        if (is_valid())
        {
            *ppInfos = m_function_infos.size() ? &m_function_infos[0] : NULL;
            *pnInfos = m_function_infos.size();

            rv = true;
        }

        return rv;
    }

    bool get_preparable_stmt(GWBUF** ppPreparable_stmt) const
    {
        bool rv = false;

        if (is_valid())
        {
            *ppPreparable_stmt = m_pPreparable_stmt;
            rv = true;
        }

        return rv;
    }

    void get_canonical(string_view* pCanonical) const
    {
        *pCanonical = m_canonical;
    }

    // PUBLIC for now at least.

    /**
     * Returns whether fields should be collected.
     *
     * @return True, if should be, false otherwise.
     */
    bool must_collect_fields() const
    {
        // We must collect if fields should be collected and they have not
        // been collected yet.
        return (m_collect & Parser::COLLECT_FIELDS) && !(m_collected & Parser::COLLECT_FIELDS);
    }

    /**
     * Returns whether a function is sequence related.
     *
     * @param zFunc_name  A function name.
     *
     * @return True, if the function is sequence related, false otherwise.
     */
    bool is_sequence_related_function(const char* zFunc_name) const
    {
        bool rv = false;

        if (m_sql_mode == Parser::SqlMode::ORACLE)
        {
            // In Oracle mode we ignore the pseudocolumns "currval" and "nextval".
            // We also exclude "lastval", the 10.3 equivalent of "currval".
            if ((strcasecmp(zFunc_name, "currval") == 0)
                || (strcasecmp(zFunc_name, "nextval") == 0)
                || (strcasecmp(zFunc_name, "lastval") == 0))
            {
                rv = true;
            }
        }

        if (!rv)
        {
            if ((strcasecmp(zFunc_name, "lastval") == 0)
                || (strcasecmp(zFunc_name, "nextval") == 0))
            {
                rv = true;
            }
        }

        return rv;
    }

    /**
     * Returns whether a field is sequence related.
     *
     * @param zDatabase  The database/schema or NULL.
     * @param zTable     The table or NULL.
     * @param zColumn    The column.
     *
     * @return True, if the field is sequence related, false otherwise.
     */
    bool is_sequence_related_field(const char* zDatabase,
                                   const char* zTable,
                                   const char* zColumn) const
    {
        return is_sequence_related_function(zColumn);
    }

    static void honour_aliases(const QcAliases* pAliases,
                               const char** pzDatabase,
                               const char** pzTable)
    {
        const char*& zDatabase = *pzDatabase;
        const char*& zTable = *pzTable;

        if (!zDatabase && zTable && pAliases)
        {
            QcAliases::const_iterator i = pAliases->find(zTable);

            if (i != pAliases->end())
            {
                const QcAliasValue& value = i->second;

                zDatabase = value.database.c_str();
                zTable = value.table.c_str();
            }
        }
    }

    // QC_FIELD_NAME or Parser::FieldInfo
    template<class T>
    class MatchFieldName : public std::unary_function<T, bool>
    {
    public:
        MatchFieldName(const char* zDatabase,
                       const char* zTable,
                       const char* zColumn)
            : m_database(zDatabase ? string_view(zDatabase) : string_view {})
            , m_table(zTable ? string_view(zTable) : string_view {})
            , m_zColumn(zColumn)
        {
            mxb_assert(zColumn);
        }

        bool operator()(const T& t)
        {
            bool rv = false;

            if (sv_case_eq(m_zColumn, t.column))
            {
                if (m_table.empty() && t.table.empty())
                {
                    mxb_assert(m_database.empty() && t.database.empty());
                    rv = true;
                }
                else if (!m_table.empty() && !t.table.empty() && sv_case_eq(m_table, t.table))
                {
                    if (m_database.empty() && t.database.empty())
                    {
                        rv = true;
                    }
                    else if (!m_database.empty()
                             && !t.database.empty()
                             && sv_case_eq(m_database, t.database))
                    {
                        rv = true;
                    }
                }
            }

            return rv;
        }

    private:
        string_view m_database;
        string_view m_table;
        const char* m_zColumn;
    };

    void update_field_info(const QcAliases* pAliases,
                           uint32_t context,
                           const char* zDatabase,
                           const char* zTable,
                           const char* zColumn,
                           const ExprList* pExclude)
    {
        mxb_assert(zColumn);

        // NOTE: This must be first, so that the type mask is properly updated
        // NOTE: in case zColumn is "currval" etc.
        if (is_sequence_related_field(zDatabase, zTable, zColumn))
        {
            m_type_mask |= mxs::sql::TYPE_WRITE;
            return;
        }

        if (!must_collect_fields())
        {
            // If field information should not be collected, or if field information
            // has already been collected, we just return.
            return;
        }

        honour_aliases(pAliases, &zDatabase, &zTable);

        MatchFieldName<Parser::FieldInfo> predicate(zDatabase, zTable, zColumn);

        vector<Parser::FieldInfo>::iterator i = find_if(m_field_infos.begin(),
                                                        m_field_infos.end(),
                                                        predicate);

        if (i == m_field_infos.end())   // If true, the field was not present already.
        {
            // If only a column is specified, but not a table or database and we
            // have a list of expressions that should be excluded, we check if the column
            // value is present in that list. This is in order to exclude the second "d" in
            // a statement like "select a as d from x where d = 2".
            if (!(zColumn && !zTable && !zDatabase && pExclude && should_exclude(zColumn, pExclude)))
            {
                Parser::FieldInfo item;

                populate_field_info(item, zDatabase, zTable, zColumn);
                item.context = context;

                m_field_infos.push_back(item);
            }
        }
        else
        {
            i->context |= context;
        }
    }

    enum class Exclude
    {
        DUAL,
        NONE
    };

    void update_names(const char* zDatabase,
                      const char* zTable,
                      const char* zAlias,
                      QcAliases* pAliases,
                      Exclude exclude = Exclude::DUAL)
    {
        bool should_collect_alias = pAliases && zAlias && should_collect(Parser::COLLECT_FIELDS);
        bool should_collect_table = should_collect_alias || should_collect(Parser::COLLECT_TABLES);
        bool should_collect_database = zDatabase
            && (should_collect_alias || should_collect(Parser::COLLECT_DATABASES));

        if (should_collect_table || should_collect_database)
        {
            string_view collected_database;
            string_view collected_table;

            size_t nDatabase = zDatabase ? strlen(zDatabase) : 0;
            size_t nTable = zTable ? strlen(zTable) : 0;

            char database[nDatabase + 1];
            char table[nTable + 1];

            if (zDatabase)
            {
                strcpy(database, zDatabase);
                exposed_sqlite3Dequote(database);
            }

            if (should_collect_table && zTable)
            {
                if (strcasecmp(zTable, "DUAL") != 0 || exclude == Exclude::NONE)
                {
                    strcpy(table, zTable);
                    exposed_sqlite3Dequote(table);

                    collected_table = update_table_names(database, nDatabase, table, nTable);
                }
            }

            if (should_collect_database)
            {
                collected_database = update_database_names(database, nDatabase);
            }

            if (pAliases && !collected_table.empty() && zAlias)
            {
                // This is a bit odd. However, for whatever reason the compiler gets
                // confused by anything less odd.
                std::pair<string, QcAliasValue> item;
                item.first = string(zAlias);
                item.second.database = string(collected_database);
                item.second.table = string(collected_table);

                pAliases->insert(item);
            }
        }
    }

    static int32_t type_check_dynamic_string(const Expr* pExpr)
    {
        int32_t type_mask = 0;

        if (pExpr)
        {
            switch (pExpr->op)
            {
            case TK_CONCAT:
                type_mask |= type_check_dynamic_string(pExpr->pLeft);
                type_mask |= type_check_dynamic_string(pExpr->pRight);
                break;

            case TK_VARIABLE:
                mxb_assert(pExpr->u.zToken);
                {
                    const char* zToken = pExpr->u.zToken;
                    if (zToken[0] == '@')
                    {
                        if (zToken[1] == '@')
                        {
                            type_mask |= mxs::sql::TYPE_SYSVAR_READ;
                        }
                        else
                        {
                            type_mask |= mxs::sql::TYPE_USERVAR_READ;
                        }
                    }
                }
                break;

            default:
                break;
            }
        }

        return type_mask;
    }

    static int string_to_truth(const char* s)
    {
        int truth = -1;

        if ((strcasecmp(s, "true") == 0) || (strcasecmp(s, "on") == 0))
        {
            truth = 1;
        }
        else if ((strcasecmp(s, "false") == 0) || (strcasecmp(s, "off") == 0))
        {
            truth = 0;
        }

        return truth;
    }

    static bool is_pure_limit(const Expr* pExpr)
    {
        // When sqlite3 parses a statement like "DELETE FROM t WHERE a IN (...) LIMIT 1"
        // the WHERE part appears to be an IN expression, but so do "DELETE FROM T LIMIT 1"
        // and "DELETE FROM T WHERE a=2 LIMIT 2" appear to be.
        // In the first case, "in" should be reported as a function that is used, but
        // in the latter case it should not be.
        // This function figures out whether we have a "DELETE FROM T LIMIT 1" kind
        // of statement.
        mxb_assert(pExpr->op == TK_IN);

        return
            pExpr->flags & EP_xIsSelect
            && ((pExpr->x.pSelect->pLimit && !pExpr->x.pSelect->pWhere)
                || (pExpr->x.pSelect->pLimit && pExpr->x.pSelect->pWhere
                    && pExpr->x.pSelect->pWhere->op != TK_IN));
    }

    void update_field_infos(QcAliases* pAliases,
                            uint32_t context,
                            int prev_token,
                            const Expr* pExpr,
                            qc_token_position_t pos,
                            const ExprList* pExclude)
    {
        const Expr* pLeft = pExpr->pLeft;
        const Expr* pRight = pExpr->pRight;
        const char* zToken = pExpr->u.zToken;

        bool ignore_exprlist = false;
        bool ignore_function = false;

        switch (pExpr->op)
        {
        case TK_ASTERISK:   // select *
            update_field_infos_from_expr(pAliases, context, pExpr, pExclude);
            break;

        case TK_DOT:    // select a.b ... select a.b.c
            update_field_infos_from_expr(pAliases, context, pExpr, pExclude);
            break;

        case TK_ID:     // select a
            update_field_infos_from_expr(pAliases, context, pExpr, pExclude);
            break;

        case TK_STRING:     // select "a" ..., for @@sql_mode containing 'ANSI_QUOTES'
            if (this_thread.options & Parser::OPTION_STRING_AS_FIELD)
            {
                const char* zColumn = pExpr->u.zToken;
                update_field_infos_from_column(pAliases, context, zColumn, pExclude);
            }
            break;

        case TK_VARIABLE:
            {
                if (zToken[0] == '@')
                {
                    if (zToken[1] == '@')
                    {
                        // TODO: This should actually be "... && (m_operation == mxs::sql::OP_SET)"
                        // TODO: but there is no mxs::sql::OP_SET at the moment.
                        if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT)
                            && (m_operation != mxs::sql::OP_SELECT))
                        {
                            m_type_mask |= mxs::sql::TYPE_GSYSVAR_WRITE;
                        }
                        else
                        {
                            if ((strcasecmp(&zToken[2], "identity") == 0)
                                || (strcasecmp(&zToken[2], "last_insert_id") == 0))
                            {
                                m_type_mask |= mxs::sql::TYPE_MASTER_READ;
                            }
                            else
                            {
                                m_type_mask |= mxs::sql::TYPE_SYSVAR_READ;
                            }
                        }
                    }
                    else
                    {
                        if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT))
                        {
                            m_type_mask |= mxs::sql::TYPE_USERVAR_WRITE;
                        }
                        else
                        {
                            m_type_mask |= mxs::sql::TYPE_USERVAR_READ;
                        }
                    }
                }
                else
                {
                    // '?' is always accepted as a positional parameter.
                    if (zToken[0] != '?')
                    {
                        // If the mode is Oracle then :N is accepted as well.
                        if (zToken[0] != ':' || this_thread.sql_mode != Parser::SqlMode::ORACLE)
                        {
                            // Everything else is unexpected, but harmless.
                            MXB_WARNING("%s reported as VARIABLE.", zToken);
                        }
                    }
                }
            }
            break;

        default:
            MXB_DEBUG("Token %d not handled explicitly.", pExpr->op);

        // Fallthrough intended.
        case TK_BETWEEN:
        case TK_CASE:
        case TK_EXISTS:
        case TK_FUNCTION:
        case TK_IN:
        case TK_SELECT:
            switch (pExpr->op)
            {
            case TK_IN:
                ignore_function = is_pure_limit(pExpr);
            case TK_EQ:
            case TK_GE:
            case TK_GT:
            case TK_LE:
            case TK_LT:
            case TK_NE:

            case TK_BETWEEN:
            case TK_BITAND:
            case TK_BITOR:
            case TK_CASE:
            case TK_CAST:
            case TK_DIV:
            case TK_ISNULL:
            case TK_MINUS:
            case TK_MOD:
            case TK_NOTNULL:
            case TK_PLUS:
            case TK_SLASH:
            case TK_STAR:
                {
                    if (!ignore_function)
                    {
                        int i = update_function_info(pAliases,
                                                     get_token_symbol(pExpr->op),
                                                     pExclude);

                        if (i != -1)
                        {
                            vector<Parser::FieldInfo>& fields = m_function_field_usage[i];

                            if (pExpr->pLeft)
                            {
                                update_function_fields(pAliases, pExpr->pLeft, pExclude, fields);
                            }

                            if (pExpr->pRight)
                            {
                                update_function_fields(pAliases, pExpr->pRight, pExclude, fields);
                            }

                            if (fields.size() != 0)
                            {
                                Parser::FunctionInfo& info = m_function_infos[i];

                                info.fields = &fields[0];
                                info.n_fields = fields.size();
                            }
                        }
                    }
                }
                break;

            case TK_REM:
                if (m_sql_mode == Parser::SqlMode::ORACLE)
                {
                    if ((pLeft && (pLeft->op == TK_ID))
                        && (pRight && (pRight->op == TK_ID))
                        && (strcasecmp(pLeft->u.zToken, "sql") == 0)
                        && (strcasecmp(pRight->u.zToken, "rowcount") == 0))
                    {
                        char sqlrowcount[13];   // strlen("sql") + strlen("%") + strlen("rowcount") + 1
                        sprintf(sqlrowcount, "%s%%%s", pLeft->u.zToken, pRight->u.zToken);

                        update_function_info(pAliases, sqlrowcount, pExclude);

                        pLeft = NULL;
                        pRight = NULL;
                    }
                    else
                    {
                        update_function_info(pAliases, get_token_symbol(pExpr->op), pExclude);
                    }
                }
                else
                {
                    update_function_info(pAliases, get_token_symbol(pExpr->op), pExclude);
                }
                break;

            case TK_UMINUS:
                break;

            case TK_FUNCTION:
                if (zToken)
                {
                    if (strcasecmp(zToken, "last_insert_id") == 0)
                    {
                        m_type_mask |= mxs::sql::TYPE_MASTER_READ;
                    }
                    else if (is_sequence_related_function(zToken))
                    {
                        m_type_mask |= mxs::sql::TYPE_WRITE;
                        ignore_exprlist = true;
                    }
                    else if (!is_builtin_readonly_function(zToken,
                                                           this_thread.version_major,
                                                           this_thread.version_minor,
                                                           this_thread.version_patch,
                                                           m_sql_mode == Parser::SqlMode::ORACLE))
                    {
                        m_type_mask |= mxs::sql::TYPE_WRITE;
                    }

                    // We exclude "row", because we cannot detect all rows the same
                    // way qc_mysqlembedded does.
                    if (!ignore_exprlist && (strcasecmp(zToken, "row") != 0))
                    {
                        update_function_info(pAliases, zToken, pExpr->x.pList, pExclude);
                    }
                }
                break;

            default:
                break;
            }

            if (pLeft)
            {
                update_field_infos(pAliases, context, pExpr->op, pExpr->pLeft, QC_TOKEN_LEFT, pExclude);
            }

            if (pRight)
            {
                update_field_infos(pAliases, context, pExpr->op, pExpr->pRight, QC_TOKEN_RIGHT, pExclude);
            }

            if (pExpr->x.pList)
            {
                switch (pExpr->op)
                {
                case TK_FUNCTION:
                    if (!ignore_exprlist)
                    {
                        update_field_infos_from_exprlist(pAliases, context, pExpr->x.pList, pExclude);
                    }
                    break;

                case TK_BETWEEN:
                case TK_CASE:
                case TK_EXISTS:
                case TK_IN:
                case TK_SELECT:
                    {
                        const char* zName = NULL;

                        switch (pExpr->op)
                        {
                        case TK_BETWEEN:
                        case TK_CASE:
                        case TK_IN:
                            if (!ignore_function)
                            {
                                zName = get_token_symbol(pExpr->op);
                            }
                            break;
                        }

                        if (pExpr->flags & EP_xIsSelect)
                        {
                            mxb_assert(pAliases);
                            update_field_infos_from_subselect(*pAliases, context, pExpr->x.pSelect, pExclude);

                            if (zName)
                            {
                                update_function_info(pAliases,
                                                     zName,
                                                     pExpr->x.pSelect->pEList,
                                                     pExclude);
                            }
                        }
                        else
                        {
                            update_field_infos_from_exprlist(pAliases, context, pExpr->x.pList, pExclude);

                            if (zName)
                            {
                                update_function_info(pAliases,
                                                     zName,
                                                     pExpr->x.pList,
                                                     pExclude);
                            }
                        }
                    }
                    break;
                }
            }
            break;
        }
    }

    static bool get_field_name(const Expr* pExpr,
                               const char** pzDatabase,
                               const char** pzTable,
                               const char** pzColumn)
    {
        const char*& zDatabase = *pzDatabase;
        const char*& zTable = *pzTable;
        const char*& zColumn = *pzColumn;

        zDatabase = NULL;
        zTable = NULL;
        zColumn = NULL;

        if (pExpr->op == TK_ASTERISK)
        {
            zColumn = (char*)"*";
        }
        else if (pExpr->op == TK_ID)
        {
            // select a from...
            zColumn = pExpr->u.zToken;
        }
        else if (pExpr->op == TK_DOT)
        {
            if (pExpr->pLeft->op == TK_ID
                && (pExpr->pRight->op == TK_ID || pExpr->pRight->op == TK_ASTERISK))
            {
                // select a.b from...
                zTable = pExpr->pLeft->u.zToken;
                if (pExpr->pRight->op == TK_ID)
                {
                    zColumn = pExpr->pRight->u.zToken;
                }
                else
                {
                    zColumn = (char*)"*";
                }
            }
            else if (pExpr->pLeft->op == TK_ID
                     && pExpr->pRight->op == TK_DOT
                     && pExpr->pRight->pLeft->op == TK_ID
                     && (pExpr->pRight->pRight->op == TK_ID || pExpr->pRight->pRight->op == TK_ASTERISK))
            {
                // select a.b.c from...
                zDatabase = pExpr->pLeft->u.zToken;
                zTable = pExpr->pRight->pLeft->u.zToken;
                if (pExpr->pRight->pRight->op == TK_ID)
                {
                    zColumn = pExpr->pRight->pRight->u.zToken;
                }
                else
                {
                    zColumn = (char*)"*";
                }
            }
        }
        else if (pExpr->op == TK_STRING)
        {
            if (this_thread.options & Parser::OPTION_STRING_ARG_AS_FIELD)
            {
                zColumn = pExpr->u.zToken;
            }
        }

        if (zColumn)
        {
            if ((pExpr->flags & EP_DblQuoted) == 0)
            {
                if ((strcasecmp(zColumn, "true") == 0) || (strcasecmp(zColumn, "false") == 0))
                {
                    zDatabase = NULL;
                    zTable = NULL;
                    zColumn = NULL;
                }
            }
        }

        return zColumn != NULL;
    }

    void update_field_infos_from_expr(QcAliases* pAliases,
                                      uint32_t context,
                                      const Expr* pExpr,
                                      const ExprList* pExclude)
    {
        const char* zDatabase;
        const char* zTable;
        const char* zColumn;

        if (get_field_name(pExpr, &zDatabase, &zTable, &zColumn))
        {
            update_field_info(pAliases, context, zDatabase, zTable, zColumn, pExclude);
        }
    }

    void update_field_infos_from_column(QcAliases* pAliases,
                                        uint32_t context,
                                        const char* zColumn,
                                        const ExprList* pExclude)
    {
        update_field_info(pAliases, context, nullptr, nullptr, zColumn, pExclude);
    }

    void update_field_infos_from_exprlist(QcAliases* pAliases,
                                          uint32_t context,
                                          const ExprList* pEList,
                                          const ExprList* pExclude)
    {
        for (int i = 0; i < pEList->nExpr; ++i)
        {
            ExprList::ExprList_item* pItem = &pEList->a[i];

            update_field_infos(pAliases, context, 0, pItem->pExpr, QC_TOKEN_MIDDLE, pExclude);
        }
    }

    void update_field_infos_from_idlist(QcAliases* pAliases,
                                        uint32_t context,
                                        const IdList* pIds,
                                        const ExprList* pExclude)
    {
        for (int i = 0; i < pIds->nId; ++i)
        {
            IdList::IdList_item* pItem = &pIds->a[i];

            update_field_info(pAliases, context, NULL, NULL, pItem->zName, pExclude);
        }
    }

    enum compound_approach_t
    {
        ANALYZE_COMPOUND_SELECTS,
        IGNORE_COMPOUND_SELECTS
    };

    bool is_significant_union(const Select* pSelect)
    {
        return ((pSelect->op == TK_UNION) || (pSelect->op == TK_ALL)) && pSelect->pPrior;
    }

    void update_field_infos_from_select(QcAliases& aliases,
                                        uint32_t context,
                                        const Select* pSelect,
                                        const ExprList* pExclude,
                                        compound_approach_t compound_approach = ANALYZE_COMPOUND_SELECTS)
    {
        if (pSelect->pSrc)
        {
            const SrcList* pSrc = pSelect->pSrc;

            for (int i = 0; i < pSrc->nSrc; ++i)
            {
                if (pSrc->a[i].zName)
                {
                    update_names(pSrc->a[i].zDatabase, pSrc->a[i].zName, pSrc->a[i].zAlias, &aliases);
                }

                if (pSrc->a[i].pSelect)
                {
                    update_field_infos_from_select(aliases,
                                                   context | Parser::FIELD_SUBQUERY,
                                                   pSrc->a[i].pSelect,
                                                   pExclude);
                }

                if (pSrc->a[i].pOn)
                {
                    update_field_infos(&aliases, context, 0, pSrc->a[i].pOn, QC_TOKEN_MIDDLE, pExclude);
                }

#ifdef PARSER_COLLECT_NAMES_FROM_USING
                // With this enabled, the affected fields of
                //    select * from (t1 as t2 left join t1 as t3 using (a)), t1;
                // will be "* a", otherwise "*". However, that "a" is used in the join
                // does not reveal its value, right?
                if (pSrc->a[i].pUsing)
                {
                    update_field_infos_from_idlist(this, aliases, pSrc->a[i].pUsing, 0, pSelect->pEList);
                }
#endif
            }
        }

        if (pSelect->pEList)
        {
            update_field_infos_from_exprlist(&aliases, context, pSelect->pEList, NULL);
        }

        if (pSelect->pWhere)
        {
            update_field_infos(&aliases,
                               context,
                               0,
                               pSelect->pWhere,
                               QC_TOKEN_MIDDLE,
                               pSelect->pEList);
        }

        if (pSelect->pGroupBy)
        {
            update_field_infos_from_exprlist(&aliases,
                                             context,
                                             pSelect->pGroupBy,
                                             pSelect->pEList);
        }

        if (pSelect->pHaving)
        {
#if defined (COLLECT_HAVING_AS_WELL)
            // A HAVING clause can only refer to fields that already have been
            // mentioned. Consequently, they need not be collected.
            update_field_infos(aliases, 0, pSelect->pHaving, 0, QC_TOKEN_MIDDLE, pSelect->pEList);
#endif
        }

        if (pSelect->pOrderBy)
        {
            update_field_infos_from_exprlist(&aliases,
                                             context,
                                             pSelect->pOrderBy,
                                             pSelect->pEList);
        }

        if (pSelect->pWith)
        {
            update_field_infos_from_with(&aliases, context, pSelect->pWith);
        }

        if (compound_approach == ANALYZE_COMPOUND_SELECTS)
        {
            if (is_significant_union(pSelect))
            {
                const Select* pPrior = pSelect->pPrior;

                while (pPrior)
                {
                    uint32_t ctx = context;

                    if (!pPrior->pPrior)
                    {
                        // The fields in the first select in a UNION are not considered to
                        // be in a union. Those names will be visible in the resultset.
                        ctx &= ~Parser::FIELD_UNION;
                    }

                    QcAliases aliases2(aliases);

                    update_field_infos_from_select(aliases2,
                                                   ctx,
                                                   pPrior,
                                                   pExclude,
                                                   IGNORE_COMPOUND_SELECTS);
                    pPrior = pPrior->pPrior;
                }
            }
        }
    }

    void update_field_infos_from_subselect(const QcAliases& existing_aliases,
                                           uint32_t context,
                                           const Select* pSelect,
                                           const ExprList* pExclude,
                                           compound_approach_t compound_approach = ANALYZE_COMPOUND_SELECTS)
    {
        QcAliases aliases(existing_aliases);

        context |= Parser::FIELD_SUBQUERY;

        update_field_infos_from_select(aliases, context, pSelect, pExclude, compound_approach);
    }

    void update_field_infos_from_with(QcAliases* pAliases, uint32_t context, const With* pWith)
    {
        for (int i = 0; i < pWith->nCte; ++i)
        {
            const With::Cte* pCte = &pWith->a[i];

            if (pCte->pSelect)
            {
                mxb_assert(pAliases);
                update_field_infos_from_subselect(*pAliases, context, pCte->pSelect, NULL);
            }
        }
    }

    void update_names_from_srclist(QcAliases* pAliases,
                                   const SrcList* pSrc)
    {
        if (pSrc)   // TODO: Figure out in what contexts pSrc can be NULL.
        {
            for (int i = 0; i < pSrc->nSrc; ++i)
            {
                if (pSrc->a[i].zName)
                {
                    update_names(pSrc->a[i].zDatabase, pSrc->a[i].zName, pSrc->a[i].zAlias, pAliases);
                }

                if (pSrc->a[i].pSelect)
                {
                    maxscaleCollectInfoFromSelect(nullptr, pSrc->a[i].pSelect, 1); // 1 denotes subselect.

                    if (pSrc->a[i].pSelect->pSrc) // The FROM clause
                    {
                        update_names_from_srclist(pAliases, pSrc->a[i].pSelect->pSrc);
                    }
                }

                if (pSrc->a[i].pOn)
                {
                    update_field_infos(pAliases, 0, 0, pSrc->a[i].pOn, QC_TOKEN_MIDDLE, NULL);
                }
            }
        }
    }

    void update_function_fields(const QcAliases* pAliases,
                                const char* zDatabase,
                                const char* zTable,
                                const char* zColumn,
                                vector<Parser::FieldInfo>& fields)
    {
        mxb_assert(zColumn);

        honour_aliases(pAliases, &zDatabase, &zTable);

        MatchFieldName<Parser::FieldInfo> predicate(zDatabase, zTable, zColumn);

        vector<Parser::FieldInfo>::iterator i = find_if(fields.begin(), fields.end(), predicate);

        if (i == fields.end())      // Not present
        {
            // TODO: Add exclusion?
            Parser::FieldInfo item;

            populate_field_info(item, zDatabase, zTable, zColumn);
            fields.push_back(item);
        }
    }

    void update_function_fields(const QcAliases* pAliases,
                                const Expr* pExpr,
                                const ExprList* pExclude,
                                vector<Parser::FieldInfo>& fields)
    {
        const char* zDatabase;
        const char* zTable;
        const char* zColumn;

        if (get_field_name(pExpr, &zDatabase, &zTable, &zColumn))
        {
            if (!zDatabase && !zTable && pExclude)
            {
                for (int i = 0; i < pExclude->nExpr; ++i)
                {
                    ExprList::ExprList_item* pItem = &pExclude->a[i];

                    if (pItem->zName && (strcasecmp(pItem->zName, zColumn) == 0))
                    {
                        get_field_name(pItem->pExpr, &zDatabase, &zTable, &zColumn);
                        break;
                    }
                }
            }

            if (zColumn)
            {
                update_function_fields(pAliases, zDatabase, zTable, zColumn, fields);
            }
        }
    }

    void update_function_fields(const QcAliases* pAliases,
                                const ExprList* pEList,
                                const ExprList* pExclude,
                                vector<Parser::FieldInfo>& fields)
    {
        for (int i = 0; i < pEList->nExpr; ++i)
        {
            ExprList::ExprList_item* pItem = &pEList->a[i];

            update_function_fields(pAliases, pItem->pExpr, pExclude, fields);
        }
    }

    int update_function_info(const QcAliases* pAliases,
                             const char* zName,
                             const Expr* pExpr,
                             const ExprList* pEList,
                             const ExprList* pExclude)

    {
        mxb_assert(zName);
        mxb_assert((!pExpr && !pEList) || (pExpr && !pEList) || (!pExpr && pEList));

        if (!(m_collect & Parser::COLLECT_FUNCTIONS) || (m_collected & Parser::COLLECT_FUNCTIONS))
        {
            // If function information should not be collected, or if function information
            // has already been collected, we just return.
            return -1;
        }

        zName = map_function_name(m_pFunction_name_mappings, zName);

        size_t i;
        for (i = 0; i < m_function_infos.size(); ++i)
        {
            Parser::FunctionInfo& function_info = m_function_infos[i];

            if (sv_case_eq(zName, function_info.name))
            {
                break;
            }
        }

        if (i == m_function_infos.size())   // If true, the function was not present already.
        {
            string_view name = get_string_view("function", zName);

            Parser::FunctionInfo item { name, nullptr, 0 };

            m_function_infos.reserve(m_function_infos.size() + 1);
            m_function_field_usage.reserve(m_function_field_usage.size() + 1);

            m_function_infos.push_back(item);
            m_function_field_usage.resize(m_function_field_usage.size() + 1);
        }

        if (pExpr || pEList)
        {
            vector<Parser::FieldInfo>& fields = m_function_field_usage[i];

            if (pExpr)
            {
                update_function_fields(pAliases, pExpr, pExclude, fields);
            }
            else
            {
                update_function_fields(pAliases, pEList, pExclude, fields);
            }

            Parser::FunctionInfo& info = m_function_infos[i];

            if (fields.size() != 0)
            {
                info.fields = &fields[0];
                info.n_fields = fields.size();
            }
        }

        return i;
    }

    int update_function_info(const QcAliases* pAliases,
                             const char* name,
                             const Expr* pExpr,
                             const ExprList* pExclude)

    {
        return update_function_info(pAliases, name, pExpr, NULL, pExclude);
    }

    int update_function_info(const QcAliases* pAliases,
                             const char* name,
                             const ExprList* pEList,
                             const ExprList* pExclude)
    {
        return update_function_info(pAliases, name, NULL, pEList, pExclude);
    }

    int update_function_info(const QcAliases* pAliases,
                             const char* name,
                             const ExprList* pExclude)
    {
        return update_function_info(pAliases, name, NULL, NULL, pExclude);
    }

    //
    // sqlite3 callbacks
    //

    void mxs_sqlite3AlterFinishAddColumn(Parse* pParse, Token* pToken)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_ALTER_TABLE;
    }

    void mxs_sqlite3AlterBeginAddColumn(Parse* pParse, SrcList* pSrcList)
    {
        mxb_assert(this_thread.initialized);

        update_names_from_srclist(NULL, pSrcList);

        exposed_sqlite3SrcListDelete(pParse->db, pSrcList);
    }

    void mxs_sqlite3Analyze(Parse* pParse, SrcList* pSrcList)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;

        update_names_from_srclist(NULL, pSrcList);

        exposed_sqlite3SrcListDelete(pParse->db, pSrcList);
    }

    void mxs_sqlite3BeginTransaction(Parse* pParse, int token, int type)
    {
        mxb_assert(this_thread.initialized);

        if ((m_sql_mode != Parser::SqlMode::ORACLE) || (token == TK_START))
        {
            m_status = Parser::Result::PARSED;
            m_type_mask = mxs::sql::TYPE_BEGIN_TRX | type;
        }
    }

    void mxs_sqlite3BeginTrigger(Parse* pParse,         /* The parse context of the CREATE TRIGGER statement
                                                         * */
                                 Token* pName1,         /* The name of the trigger */
                                 Token* pName2,         /* The name of the trigger */
                                 int tr_tm,             /* One of TK_BEFORE, TK_AFTER, TK_INSTEAD */
                                 int op,                /* One of TK_INSERT, TK_UPDATE, TK_DELETE */
                                 IdList* pColumns,      /* column list if this is an UPDATE OF trigger */
                                 SrcList* pTableName,   /* The name of the table/view the trigger applies to
                                                         * */
                                 Expr* pWhen,           /* WHEN clause */
                                 int isTemp,            /* True if the TEMPORARY keyword is present */
                                 int noErr)             /* Suppress errors if the trigger already exists */
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;

        if (pTableName)
        {
            for (size_t i = 0; i < pTableName->nAlloc; ++i)
            {
                const SrcList::SrcList_item* pItem = &pTableName->a[i];

                if (pItem->zName)
                {
                    update_names(pItem->zDatabase, pItem->zName, pItem->zAlias, NULL);
                }
            }
        }

        // We need to call this, otherwise finish trigger will not be called.
        exposed_sqlite3BeginTrigger(pParse,
                                    pName1,
                                    pName2,
                                    tr_tm,
                                    op,
                                    pColumns,
                                    pTableName,
                                    pWhen,
                                    isTemp,
                                    noErr);
    }

    void mxs_sqlite3CommitTransaction(Parse* pParse)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_COMMIT;
    }

    void mxs_sqlite3CreateIndex(Parse* pParse,      /* All information about this parse */
                                Token* pName1,      /* First part of index name. May be NULL */
                                Token* pName2,      /* Second part of index name. May be NULL */
                                SrcList* pTblName,  /* Table to index. Use pParse->pNewTable if 0 */
                                ExprList* pList,    /* A list of columns to be indexed */
                                int onError,        /* OE_Abort, OE_Ignore, OE_Replace, or OE_None */
                                Token* pStart,      /* The CREATE token that begins this statement */
                                Expr* pPIWhere,     /* WHERE clause for partial indices */
                                int sortOrder,      /* Sort order of primary key when pList==NULL */
                                int ifNotExist)     /* Omit error if index already exists */
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_CREATE;

        if (pTblName)
        {
            update_names_from_srclist(NULL, pTblName);
        }
        else if (pParse->pNewTable)
        {
            update_names(NULL, pParse->pNewTable->zName, NULL, NULL);
        }

        exposed_sqlite3ExprDelete(pParse->db, pPIWhere);
        exposed_sqlite3ExprListDelete(pParse->db, pList);
        exposed_sqlite3SrcListDelete(pParse->db, pTblName);
    }

    void mxs_sqlite3CreateView(Parse* pParse,       /* The parsing context */
                               Token* pBegin,       /* The CREATE token that begins the statement */
                               Token* pName1,       /* The token that holds the name of the view */
                               Token* pName2,       /* The token that holds the name of the view */
                               ExprList* pCNames,   /* Optional list of view column names */
                               Select* pSelect,     /* A SELECT statement that will become the new view */
                               int isTemp,          /* TRUE for a TEMPORARY view */
                               int noErr)           /* Suppress error messages if VIEW already exists */
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_CREATE;

        const Token* pName = pName2->z ? pName2 : pName1;
        const Token* pDatabase = pName2->z ? pName1 : NULL;

        char name[pName->n + 1];
        memcpy(name, pName->z, pName->n);
        name[pName->n] = 0;

        QcAliases aliases;

        if (pDatabase)
        {
            char database[pDatabase->n + 1];
            memcpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;

            update_names(database, name, NULL, &aliases);
        }
        else
        {
            update_names(NULL, name, NULL, &aliases);
        }

        if (pSelect)
        {
            uint32_t context = 0;
            update_field_infos_from_select(aliases, context, pSelect, NULL);
        }

        exposed_sqlite3ExprListDelete(pParse->db, pCNames);
        // pSelect is deleted in parse.y
    }

    void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;

        if (m_operation != mxs::sql::OP_EXPLAIN)
        {
            m_type_mask = mxs::sql::TYPE_WRITE;
            m_operation = mxs::sql::OP_DELETE;

            QcAliases aliases;

            if (pUsing)
            {
                // Walk through the using declaration and update
                // table and database names.
                for (int i = 0; i < pUsing->nSrc; ++i)
                {
                    const SrcList::SrcList_item* pItem = &pUsing->a[i];

                    if (pItem->pSelect)
                    {
                        maxscaleCollectInfoFromSelect(nullptr, pItem->pSelect, 1); // 1 denotes subselect.
                    }

                    update_names(pItem->zDatabase, pItem->zName, pItem->zAlias, &aliases);
                }

                // Walk through the tablenames while excluding alias
                // names from the using declaration.
                for (int i = 0; i < pTabList->nSrc; ++i)
                {
                    const SrcList::SrcList_item* pTable = &pTabList->a[i];
                    mxb_assert(pTable->zName);
                    int j = 0;
                    bool isSame = false;

                    do
                    {
                        SrcList::SrcList_item* pItem = &pUsing->a[j++];

                        if (pItem->zName && (strcasecmp(pTable->zName, pItem->zName) == 0))
                        {
                            isSame = true;
                        }
                        else if (pItem->zAlias && (strcasecmp(pTable->zName, pItem->zAlias) == 0))
                        {
                            isSame = true;
                        }
                    }
                    while (!isSame && (j < pUsing->nSrc));

                    if (!isSame)
                    {
                        // No alias name, update the table name.
                        update_names(pTable->zDatabase, pTable->zName, NULL, &aliases);
                    }
                }
            }
            else
            {
                update_names_from_srclist(&aliases, pTabList);
            }

            if (pWhere)
            {
                uint32_t context = 0;
                update_field_infos(&aliases, context, 0, pWhere, QC_TOKEN_MIDDLE, 0);
            }
        }

        exposed_sqlite3ExprDelete(pParse->db, pWhere);
        exposed_sqlite3SrcListDelete(pParse->db, pTabList);
        exposed_sqlite3SrcListDelete(pParse->db, pUsing);
    }

    void mxs_sqlite3DropIndex(Parse* pParse, SrcList* pName, SrcList* pTable, int bits)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_DROP;

        update_names_from_srclist(NULL, pTable);

        exposed_sqlite3SrcListDelete(pParse->db, pName);
        exposed_sqlite3SrcListDelete(pParse->db, pTable);
    }

    void mxs_sqlite3DropTable(Parse* pParse, SrcList* pName, int isView, int noErr, int isTemp)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_DROP;
        if (!isView)
        {
            m_operation = mxs::sql::OP_DROP_TABLE;
        }
        update_names_from_srclist(NULL, pName);

        exposed_sqlite3SrcListDelete(pParse->db, pName);
    }

    void mxs_sqlite3EndTable(Parse* pParse,     /* Parse context */
                             Token* pCons,      /* The ',' token after the last column defn. */
                             Token* pEnd,       /* The ')' before options in the CREATE TABLE */
                             u8 tabOpts,        /* Extra table options. Usually 0. */
                             Select* pSelect,   /* Select from a "CREATE ... AS SELECT" */
                             SrcList* pOldTable)/* The old table in "CREATE ... LIKE OldTable" */
    {
        mxb_assert(this_thread.initialized);

        if (pSelect)
        {
            QcAliases aliases;
            uint32_t context = 0;
            update_field_infos_from_select(aliases, context, pSelect, NULL);
        }
        else if (pOldTable)
        {
            update_names_from_srclist(NULL, pOldTable);
            exposed_sqlite3SrcListDelete(pParse->db, pOldTable);
        }
    }

    void mxs_sqlite3Insert(Parse* pParse,
                           SrcList* pTabList,
                           Select* pSelect,
                           IdList* pColumns,
                           int onError,
                           ExprList* pSet)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;

        if (m_operation != mxs::sql::OP_EXPLAIN)
        {
            m_type_mask = mxs::sql::TYPE_WRITE;
            m_operation = mxs::sql::OP_INSERT;
            mxb_assert(pTabList);
            mxb_assert(pTabList->nSrc >= 1);

            QcAliases aliases;
            uint32_t context = 0;

            update_names_from_srclist(&aliases, pTabList);

            if (pColumns)
            {
                update_field_infos_from_idlist(&aliases, context, pColumns, NULL);

                int i = update_function_info(&aliases, "=", NULL);

                if (i != -1)
                {
                    vector<Parser::FieldInfo>& fields = m_function_field_usage[i];

                    for (int j = 0; j < pColumns->nId; ++j)
                    {
                        update_function_fields(&aliases, NULL, NULL, pColumns->a[j].zName, fields);
                    }

                    if (fields.size() != 0)
                    {
                        Parser::FunctionInfo& info = m_function_infos[i];

                        info.fields = &fields[0];
                        info.n_fields = fields.size();
                    }
                }
            }

            if (pSelect)
            {
                update_field_infos_from_select(aliases, context, pSelect, NULL);
            }

            if (pSet)
            {
                update_field_infos_from_exprlist(&aliases, context, pSet, NULL);
            }
        }

        exposed_sqlite3SrcListDelete(pParse->db, pTabList);
        exposed_sqlite3IdListDelete(pParse->db, pColumns);
        exposed_sqlite3ExprListDelete(pParse->db, pSet);
        exposed_sqlite3SelectDelete(pParse->db, pSelect);
    }

    void mxs_sqlite3RollbackTransaction(Parse* pParse)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_ROLLBACK;
    }

    void mxs_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;

        if (m_operation != mxs::sql::OP_EXPLAIN)
        {
            m_operation = mxs::sql::OP_SELECT;

            maxscaleCollectInfoFromSelect(pParse, p, 0);
        }
        // NOTE: By convention, the select is deleted in parse.y.
    }

    void mxs_sqlite3StartTable(Parse* pParse,   /* Parser context */
                               Token* pName1,   /* First part of the name of the table or view */
                               Token* pName2,   /* Second part of the name of the table or view */
                               int isTemp,      /* True if this is a TEMP table */
                               int isView,      /* True if this is a VIEW */
                               int isVirtual,   /* True if this is a VIRTUAL table */
                               int noErr)       /* Do nothing if table already exists */
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_operation = mxs::sql::OP_CREATE_TABLE;
        m_type_mask = mxs::sql::TYPE_WRITE;

        if (isTemp)
        {
            m_type_mask |= mxs::sql::TYPE_CREATE_TMP_TABLE;
        }

        const Token* pName = pName2->z ? pName2 : pName1;
        const Token* pDatabase = pName2->z ? pName1 : NULL;

        char name[pName->n + 1];
        memcpy(name, pName->z, pName->n);
        name[pName->n] = 0;

        if (pDatabase)
        {
            char database[pDatabase->n + 1];
            memcpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;

            update_names(database, name, NULL, NULL, Exclude::NONE);
        }
        else
        {
            update_names(NULL, name, NULL, NULL, Exclude::NONE);
        }

        if (m_collect & Parser::COLLECT_TABLES)
        {
            // If information is collected in several passes, then we may
            // this information already.
            if (m_created_table_name.empty())
            {
                m_created_table_name = m_table_names[0].table;
            }
            else
            {
                mxb_assert(m_collect != m_collected);
                mxb_assert(m_created_table_name == m_table_names[0].table);
            }
        }
    }

    void mxs_sqlite3Update(Parse* pParse, SrcList* pTabList, ExprList* pChanges, Expr* pWhere, int onError)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;

        if (m_operation != mxs::sql::OP_EXPLAIN)
        {
            QcAliases aliases;
            uint32_t context = 0;

            m_type_mask = mxs::sql::TYPE_WRITE;
            m_operation = mxs::sql::OP_UPDATE;
            update_names_from_srclist(&aliases, pTabList);

            if (pChanges)
            {
                for (int i = 0; i < pChanges->nExpr; ++i)
                {
                    ExprList::ExprList_item* pItem = &pChanges->a[i];

                    update_field_infos(&aliases,
                                       context,
                                       0,
                                       pItem->pExpr,
                                       QC_TOKEN_MIDDLE,
                                       NULL);
                }
            }

            if (pWhere)
            {
                update_field_infos(&aliases, context, 0, pWhere, QC_TOKEN_MIDDLE, pChanges);
            }
        }

        exposed_sqlite3SrcListDelete(pParse->db, pTabList);
        exposed_sqlite3ExprListDelete(pParse->db, pChanges);
        exposed_sqlite3ExprDelete(pParse->db, pWhere);
    }

    void mxs_sqlite3Savepoint(Parse* pParse, int op, Token* pName)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
    }

    void maxscaleCollectInfoFromSelect(Parse* pParse, Select* pSelect, int sub_select)
    {
        mxb_assert(this_thread.initialized);

        if (pSelect->pInto)
        {
            const ExprList* pInto = pSelect->pInto;
            mxb_assert(pInto->nExpr >= 1);

            if ((pInto->nExpr == 1)
                && (pInto->a[0].zName)
                && ((strcmp(pInto->a[0].zName, ":DUMPFILE:") == 0)
                    || (strcmp(pInto->a[0].zName, ":OUTFILE:") == 0)))
            {
                // If there is exactly one expression that has a name that is either
                // ":DUMPFILE:" or ":OUTFILE:" then it's a SELECT ... INTO OUTFILE|DUMPFILE
                // and the statement needs to go to master.
                // See in parse.y, the rule for select_into.
                m_type_mask = mxs::sql::TYPE_WRITE;
            }
            else
            {
                // If there's a single variable, then it's a write.
                // mysql embedded considers it a system var write.
                m_type_mask = mxs::sql::TYPE_GSYSVAR_WRITE;
            }

            // Also INTO {OUTFILE|DUMPFILE} will be typed as mxs::sql::TYPE_GSYSVAR_WRITE.
        }
        else
        {
            m_type_mask |= mxs::sql::TYPE_READ;
        }

        QcAliases aliases;
        uint32_t context = is_significant_union(pSelect) ? Parser::FIELD_UNION : 0;
        update_field_infos_from_select(aliases, context, pSelect, NULL);
    }

    void maxscaleAlterTable(Parse* pParse,              /* Parser context. */
                            mxs_alter_t command,
                            SrcList* pSrc,              /* The table to rename. */
                            Token* pName)               /* The new table name (RENAME). */
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_ALTER_TABLE;

        switch (command)
        {
        case MXS_ALTER_DISABLE_KEYS:
            update_names_from_srclist(NULL, pSrc);
            break;

        case MXS_ALTER_ENABLE_KEYS:
            update_names_from_srclist(NULL, pSrc);
            break;

        case MXS_ALTER_RENAME:
            update_names_from_srclist(NULL, pSrc);
            break;

        default:
            ;
        }

        exposed_sqlite3SrcListDelete(pParse->db, pSrc);
    }

    void maxscaleCall(Parse* pParse, SrcList* pName, ExprList* pExprList)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_CALL;

        if (pExprList)
        {
            QcAliases aliases;
            uint32_t context = 0;
            update_field_infos_from_exprlist(&aliases, context, pExprList, NULL);
        }

        exposed_sqlite3SrcListDelete(pParse->db, pName);
        exposed_sqlite3ExprListDelete(pParse->db, pExprList);
    }

    void maxscaleCheckTable(Parse* pParse, SrcList* pTables)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;

        update_names_from_srclist(NULL, pTables);

        exposed_sqlite3SrcListDelete(pParse->db, pTables);
    }

    void maxscaleCreateSequence(Parse* pParse, Token* pDatabase, Token* pTable)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;

        const char* zDatabase = NULL;
        char database[pDatabase ? pDatabase->n + 1 : 1];

        if (pDatabase)
        {
            memcpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;

            zDatabase = database;
        }

        char table[pTable->n + 1];
        memcpy(table, pTable->z, pTable->n);
        table[pTable->n] = 0;

        update_names(zDatabase, table, NULL, NULL);
    }

    int maxscaleComment()
    {
        // We are regularly parsing if the thread has been initialized.
        // In that case # should be interpreted as the start of a comment,
        // otherwise it should not.
        int regular_parsing = false;

        if (this_thread.initialized)
        {
            regular_parsing = true;

            if (m_status == Parser::Result::INVALID)
            {
                m_status = Parser::Result::PARSED;
                m_type_mask = mxs::sql::TYPE_READ;
            }
        }

        return regular_parsing;
    }

    void maxscaleDeclare(Parse* pParse)
    {
        mxb_assert(this_thread.initialized);

        if (m_sql_mode != Parser::SqlMode::ORACLE)
        {
            m_status = Parser::Result::INVALID;
        }
    }

    void maxscaleDeallocate(Parse* pParse, Token* pName)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_DEALLOC_PREPARE;

        // If information is collected in several passes, then we may
        // this information already.
        if (m_prepare_name.empty())
        {
            m_prepare_name = get_string_view("prepare_name", string(pName->z, pName->n).c_str());
        }
        else
        {
            mxb_assert(m_collect != m_collected);
            mxb_assert(sv_case_eq(m_prepare_name, string_view(pName->z, pName->n)));
        }
    }

    void maxscaleDo(Parse* pParse, ExprList* pEList)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = (mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE);

        exposed_sqlite3ExprListDelete(pParse->db, pEList);
    }

    void maxscaleDrop(Parse* pParse, int what, Token* pDatabase, Token* pName)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_DROP;

        switch (what)
        {
        case MXS_DROP_DATABASE:
            {
#ifdef TODO_SPECIFIC_OP_FOR_DROP_DATABASE_ADDED
                // TODO: As there is only mxs::sql::OP_DROP, you can't be fully
                // TODO: certain what a returned database actually refers to
                // TODO: so better not to provide a name until there is a
                // TODO: specific op.
                update_database_names(pDatabase->z, pDatabase->n);
#endif
            }
            break;

        case MXS_DROP_SEQUENCE:
            {
                const char* zDatabase = NULL;
                char database[pDatabase ? pDatabase->n + 1 : 1];

                if (pDatabase)
                {
                    memcpy(database, pDatabase->z, pDatabase->n);
                    database[pDatabase->n] = 0;

                    zDatabase = database;
                }

                char table[pName->n + 1];
                memcpy(table, pName->z, pName->n);
                table[pName->n] = 0;

                update_names(zDatabase, table, NULL, NULL);
            }
            break;
        }
    }

    void maxscaleExecute(Parse* pParse, Token* pName, int type_mask)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = (mxs::sql::TYPE_WRITE | type_mask);
        m_operation = mxs::sql::OP_EXECUTE;

        // If information is collected in several passes, then we may
        // this information already.
        if (m_prepare_name.empty())
        {
            m_prepare_name = get_string_view("prepare_name", string(pName->z, pName->n).c_str());
        }
        else
        {
            mxb_assert(m_collect != m_collected);
            mxb_assert(sv_case_eq(m_prepare_name, string_view(pName->z, pName->n)));
        }
    }

    void maxscaleExecuteImmediate(Parse* pParse, Token* pName, ExprSpan* pExprSpan, int type_mask)
    {
        mxb_assert(this_thread.initialized);

        if (m_sql_mode == Parser::SqlMode::ORACLE)
        {
            // This should be "EXECUTE IMMEDIATE ...", but as "IMMEDIATE" is not
            // checked by the parser we do it here.

            static const char IMMEDIATE[] = "IMMEDIATE";

            if ((pName->n == sizeof(IMMEDIATE) - 1) && (strncasecmp(pName->z, IMMEDIATE, pName->n)) == 0)
            {
                m_status = Parser::Result::PARSED;
                m_type_mask = (mxs::sql::TYPE_WRITE | type_mask);
                m_type_mask |= type_check_dynamic_string(pExprSpan->pExpr);
            }
            else
            {
                m_status = Parser::Result::INVALID;
            }
        }
        else
        {
            m_status = Parser::Result::INVALID;
        }

        exposed_sqlite3ExprDelete(pParse->db, pExprSpan->pExpr);
    }

    void maxscaleExplainTable(Parse* pParse, SrcList* pList)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_READ;
        m_operation = mxs::sql::OP_SHOW;

        for (int i = 0; i < pList->nSrc; ++i)
        {
            if (pList->a[i].zName)
            {
                update_names(pList->a[i].zDatabase, pList->a[i].zName, pList->a[i].zAlias, nullptr);
            }
        }

        exposed_sqlite3SrcListDelete(pParse->db, pList);
    }

    void maxscaleExplain(Parse* pParse)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_READ;
        m_operation = mxs::sql::OP_EXPLAIN;
    }

    void maxscaleFlush(Parse* pParse, Token* pWhat)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
    }

    void maxscaleHandler(Parse* pParse, mxs_handler_t type, SrcList* pFullName, Token* pName)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;

        switch (type)
        {
        case MXS_HANDLER_OPEN:
            {
                m_type_mask = mxs::sql::TYPE_WRITE;

                mxb_assert(pFullName->nSrc == 1);
                const SrcList::SrcList_item* pItem = &pFullName->a[0];

                update_names(pItem->zDatabase, pItem->zName, pItem->zAlias, NULL);
            }
            break;

        case MXS_HANDLER_CLOSE:
            {
                m_type_mask = mxs::sql::TYPE_WRITE;

                char zName[pName->n + 1];
                memcpy(zName, pName->z, pName->n);
                zName[pName->n] = 0;

                update_names("*any*", zName, NULL, NULL);
            }
            break;

        default:
            mxb_assert(!true);
        }

        exposed_sqlite3SrcListDelete(pParse->db, pFullName);
    }

    void maxscaleLoadData(Parse* pParse, SrcList* pFullName, int local)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = local ? mxs::sql::OP_LOAD_LOCAL : mxs::sql::OP_LOAD;

        if (pFullName)
        {
            update_names_from_srclist(NULL, pFullName);

            exposed_sqlite3SrcListDelete(pParse->db, pFullName);
        }
    }

    void maxscaleLock(Parse* pParse, mxs_lock_t type, SrcList* pTables)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;

        if (pTables)
        {
            update_names_from_srclist(NULL, pTables);

            exposed_sqlite3SrcListDelete(pParse->db, pTables);
        }
    }

    void maxscaleOptimize(Parse* pParse, SrcList* pTables)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;

        if (pTables)
        {
            update_names_from_srclist(NULL, pTables);

            exposed_sqlite3SrcListDelete(pParse->db, pTables);
        }
    }

    void maxscaleKill(Parse* pParse, MxsKill* pKill)
    {
        mxb_assert(this_thread.initialized);
        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_KILL;
        m_kill.soft = pKill->soft;
        m_kill.user = pKill->user;

        switch (pKill->type)
        {
        case MXS_KILL_TYPE_CONNECTION:
            m_kill.type = Parser::KillType::CONNECTION;
            break;

        case MXS_KILL_TYPE_QUERY:
            m_kill.type = Parser::KillType::QUERY;
            break;

        case MXS_KILL_TYPE_QUERY_ID:
            m_kill.type = Parser::KillType::QUERY_ID;
            break;
        }

        m_kill.target.assign(pKill->pTarget->z, pKill->pTarget->n);
        exposed_sqlite3Dequote(&m_kill.target[0]);
        m_kill.target.resize(strlen(m_kill.target.c_str()));
    }

    int maxscaleTranslateKeyword(int token)
    {
        switch (token)
        {
        case TK_CHARSET:
        case TK_DO:
        case TK_HANDLER:
            if (m_sql_mode == Parser::SqlMode::ORACLE)
            {
                // The keyword is translated, but only if it not used
                // as the first keyword. Matters for DO and HANDLER.
                if (m_keyword_1)
                {
                    token = TK_ID;
                }
            }
            break;

        default:
            break;
        }

        return token;
    }

    /**
     * Register the tokenization of a keyword.
     *
     * @param token A keyword code (check generated parse.h)
     *
     * @return Non-zero if all input should be consumed, 0 otherwise.
     */
    int maxscaleKeyword(int token)
    {
        int rv = 0;

        // This function is called for every keyword the sqlite3 parser encounters.
        // We will store in m_keyword_{1|2} the first and second keyword that
        // are encountered, and when they _are_ encountered, we make an educated
        // deduction about the statement. We can make that deduction only the first
        // (and second) time we see a keyword, so that we don't get confused by a
        // statement like "CREATE TABLE ... AS SELECT ...".
        // Since m_keyword_{1|2} is initialized with 0, well, if it is 0 then
        // we have not seen the {1st|2nd} keyword yet.

        if (!m_keyword_1)
        {
            m_keyword_1 = token;

            switch (m_keyword_1)
            {
            case TK_ALTER:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_ALTER;
                break;

            case TK_ANALYZE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_READ;
                m_operation = mxs::sql::OP_EXPLAIN;
                break;

            case TK_BEGIN:
            case TK_DECLARE:
            case TK_FOR:
                if (m_sql_mode == Parser::SqlMode::ORACLE)
                {
                    // The beginning of a BLOCK. We'll assume it is in a single
                    // COM_QUERY packet and hence one GWBUF.
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_WRITE;
                    // Return non-0 to cause the entire input to be consumed.
                    rv = 1;
                }
                break;

            case TK_CALL:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_CREATE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_CREATE;
                break;

            case TK_DELETE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_DELETE;
                break;

            case TK_DESC:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_READ;
                m_operation = mxs::sql::OP_EXPLAIN;
                break;

            case TK_DROP:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_DROP;
                break;

            case TK_EXECUTE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_EXPLAIN:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_READ;
                m_operation = mxs::sql::OP_EXPLAIN;
                break;

            case TK_GRANT:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_GRANT;
                break;

            case TK_HANDLER:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_INSERT:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_INSERT;
                break;

            case TK_LOCK:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_OPTIMIZE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_PREPARE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_PREPARE_NAMED_STMT;
                break;

            case TK_REPLACE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_INSERT;
                break;

            case TK_REVOKE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_REVOKE;
                break;

            case TK_RESET:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_SELECT:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_READ;
                m_operation = mxs::sql::OP_SELECT;
                break;

            case TK_SET:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_SESSION_WRITE;
                m_operation = mxs::sql::OP_SET;
                break;

            case TK_SHOW:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_READ;
                m_operation = mxs::sql::OP_SHOW;
                break;

            case TK_START:
                // Will produce the right info for START SLAVE.
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_UNLOCK:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_UPDATE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                m_operation = mxs::sql::OP_UPDATE;
                break;

            case TK_TRUNCATE:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case TK_XA:
                m_status = Parser::Result::TOKENIZED;
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            default:
                ;
            }
        }
        else if (!m_keyword_2)
        {
            m_keyword_2 = token;

            switch (m_keyword_1)
            {
            case TK_ALTER:
                if (m_keyword_2 == TK_TABLE)
                {
                    m_operation = mxs::sql::OP_ALTER_TABLE;
                }
                break;

            case TK_CHECK:
                if (m_keyword_2 == TK_TABLE)
                {
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_WRITE;
                }
                break;

            case TK_CREATE:
                if (m_keyword_2 == TK_TABLE)
                {
                    m_operation = mxs::sql::OP_CREATE_TABLE;
                }
                break;

            case TK_DEALLOCATE:
                if (m_keyword_2 == TK_PREPARE)
                {
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_SESSION_WRITE;
                }
                break;

            case TK_DROP:
                if (m_keyword_2 == TK_TABLE)
                {
                    m_operation = mxs::sql::OP_DROP_TABLE;
                }
                break;

            case TK_LOAD:
                if (m_keyword_2 == TK_DATA)
                {
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_WRITE;
                    m_operation = mxs::sql::OP_LOAD;
                }
                break;

            case TK_RENAME:
                if (m_keyword_2 == TK_TABLE)
                {
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_WRITE;
                }
                break;

            case TK_SET:
                if (m_keyword_2 == TK_PASSWORD)
                {
                    m_type_mask = mxs::sql::TYPE_WRITE;
                }
                else if (m_keyword_2 == TK_STATEMENT)
                {
                    m_type_mask = mxs::sql::TYPE_UNKNOWN; // aka 0
                }
                break;

            case TK_START:
                switch (m_keyword_2)
                {
                case TK_TRANSACTION:
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_BEGIN_TRX;
                    break;

                default:
                    break;
                }
                break;

            case TK_SHOW:
                switch (m_keyword_2)
                {
                case TK_DATABASES_KW:
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_SHOW_DATABASES;
                    break;

                case TK_TABLES:
                    m_status = Parser::Result::TOKENIZED;
                    m_type_mask = mxs::sql::TYPE_SHOW_TABLES;
                    break;

                default:
                    break;
                }
            }
        }

        return rv;
    }

    void maxscaleSetStatusCap(Parser::Result cap)
    {
        m_status_cap = cap;
    }

    void maxscaleRenameTable(Parse* pParse, SrcList* pTables)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;

        for (int i = 0; i < pTables->nSrc; ++i)
        {
            const SrcList::SrcList_item* pItem = &pTables->a[i];

            mxb_assert(pItem->zName);
            mxb_assert(pItem->zAlias);

            update_names(pItem->zDatabase, pItem->zName, NULL, NULL);
            update_names(NULL, pItem->zAlias, NULL, NULL);      // The new name is passed in the alias field.
        }

        exposed_sqlite3SrcListDelete(pParse->db, pTables);
    }

    void maxscalePrepare(Parse* pParse, Token* pName, Expr* pStmt)
    {
        mxb_assert(this_thread.initialized);

        switch (pStmt->op)
        {
        case TK_STRING:
        case TK_VARIABLE:
            m_status = Parser::Result::PARSED;
            break;

        default:
            m_status = Parser::Result::PARTIALLY_PARSED;
            break;
        }

        m_type_mask = mxs::sql::TYPE_PREPARE_NAMED_STMT;

        // If information is collected in several passes, then we may
        // this information already.
        if (m_prepare_name.empty())
        {
            m_prepare_name = get_string_view("prepare_name", string(pName->z, pName->n).c_str());

            if (pStmt->op == TK_STRING)
            {
                const char* zStmt = pStmt->u.zToken;
                mxb_assert(zStmt);

                mxb_assert(this_thread.pHelper);
                m_pPreparable_stmt = new GWBUF(this_thread.pHelper->create_packet(zStmt));
            }
        }
        else
        {
            mxb_assert(m_collect != m_collected);
            mxb_assert(sv_case_eq(m_prepare_name, string_view(pName->z, pName->n)));
        }

        exposed_sqlite3ExprDelete(pParse->db, pStmt);
    }

    void maxscalePrivileges(Parse* pParse, int kind)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;

        switch (kind)
        {
        case TK_GRANT:
            m_operation = mxs::sql::OP_GRANT;
            break;

        case TK_REVOKE:
            m_operation = mxs::sql::OP_REVOKE;
            break;

        default:
            mxb_assert(!true);
        }
    }

    void maxscaleReset(Parse* pParse, int what)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;

        switch (what)
        {
        case MXS_RESET_QUERY_CACHE:
            m_type_mask = mxs::sql::TYPE_SESSION_WRITE;
            break;

        default:
            mxb_assert(!true);
        }
    }

    void maxscaleOracleAssign(Parse* pParse, Token* pVariable, Expr* pValue)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask |= mxs::sql::TYPE_SESSION_WRITE;
        m_type_mask |= mxs::sql::TYPE_GSYSVAR_WRITE;
        m_operation = mxs::sql::OP_SET;

        exposed_sqlite3ExprDelete(pParse->db, pValue);
    }

    void maxscaleSet(Parse* pParse, int scope, mxs_set_t kind, ExprList* pList)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        // The following must be set anew as there will be no SET in case of
        // Oracle's "var := 1", in which case maxscaleKeyword() is never called.
        m_type_mask |= mxs::sql::TYPE_SESSION_WRITE;
        m_operation = mxs::sql::OP_SET;

        switch (kind)
        {
        case MXS_SET_VARIABLES:
            break;

        case MXS_SET_DEFAULT_ROLE:
            m_type_mask = mxs::sql::TYPE_WRITE;
            break;

        default:
            mxb_assert(!true);
            break;
        }

        // TODO: This isn't needed anymore and should be removed
        exposed_sqlite3ExprListDelete(pParse->db, pList);
    }

    void maxscaleSetPassword(Parse* pParse)
    {
        m_status = Parser::Result::PARSED;
        // Not a session write because that would break replication - see MXS-2713.
        m_type_mask |= mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_SET;
    }

    void maxscaleSetVariable(Parse* pParse, int scope, Expr* pExpr)
    {
        switch (pExpr->op)
        {
        case TK_CHARACTER:
        case TK_NAMES:
            break;

        case TK_EQ:
            {
                const Expr* pEq = pExpr;
                const Expr* pVariable;
                const Expr* pValue = pEq->pRight;

                // pEq->pLeft is either TK_DOT, TK_VARIABLE or TK_ID. If it's TK_DOT,
                // then pEq->pLeft->pLeft is either TK_VARIABLE or TK_ID and pEq->pLeft->pRight
                // is either TK_DOT, TK_VARIABLE or TK_ID.

                pVariable = pEq->pLeft;

                // But first we explicitly check for the case "SET PASSWORD ..."
                if (pVariable->op == TK_ID && strcasecmp(pVariable->u.zToken, "password") == 0)
                {
                    // Even though SET PASSWORD looks like a session command it
                    // is not, the password change will be replicated to slaves.
                    m_type_mask = mxs::sql::TYPE_WRITE;
                    break;
                }

                // Now find the left-most part.
                while (pVariable->op == TK_DOT)
                {
                    pVariable = pVariable->pLeft;
                    mxb_assert(pVariable);
                }

                // Check what kind of variable it is.
                size_t n_at = 0;
                const char* zName = pVariable->u.zToken;

                while (*zName == '@')
                {
                    ++n_at;
                    ++zName;
                }

                if (n_at == 1)
                {
                    m_type_mask |= mxs::sql::TYPE_USERVAR_WRITE;
                }
                else
                {
                    m_type_mask |= mxs::sql::TYPE_GSYSVAR_WRITE;

                    if (n_at == 2 && strcasecmp(zName, "GLOBAL") == 0)
                    {
                        scope = TK_GLOBAL;
                    }
                }

                // Set pVariable to point to the rightmost part of the name.
                pVariable = pEq->pLeft;
                while (pVariable->op == TK_DOT)
                {
                    pVariable = pVariable->pRight;
                }

                mxb_assert((pVariable->op == TK_VARIABLE) || (pVariable->op == TK_ID));

                if (n_at != 1)
                {
                    // If it's not a user-variable we need to check whether it might
                    // be 'autocommit'.
                    const char* zName = pVariable->u.zToken;

                    while (*zName == '@')
                    {
                        ++zName;
                    }

                    // As pVariable points to the rightmost part, we'll catch both
                    // "autocommit" and "@@global.autocommit".
                    if (strcasecmp(zName, "autocommit") == 0)
                    {
                        int enable = -1;

                        switch (pValue->op)
                        {
                        case TK_INTEGER:
                            if (pValue->u.iValue == 1)
                            {
                                enable = 1;
                            }
                            else if (pValue->u.iValue == 0)
                            {
                                enable = 0;
                            }
                            break;

                        case TK_ID:
                            enable = string_to_truth(pValue->u.zToken);
                            break;

                        default:
                            break;
                        }

                        if (scope != TK_GLOBAL)
                        {
                            switch (enable)
                            {
                            case 0:
                                m_type_mask |= mxs::sql::TYPE_BEGIN_TRX;
                                m_type_mask |= mxs::sql::TYPE_DISABLE_AUTOCOMMIT;
                                break;

                            case 1:
                                m_type_mask |= mxs::sql::TYPE_ENABLE_AUTOCOMMIT;
                                m_type_mask |= mxs::sql::TYPE_COMMIT;
                                break;

                            default:
                                break;
                            }
                        }
                    }
                }

                if (pValue->op == TK_SELECT)
                {
                    QcAliases aliases;
                    uint32_t context = 0;
                    update_field_infos_from_select(aliases, context, pValue->x.pSelect, NULL);
                }
            }
            break;

        default:
            mxb_assert(!true);
        }
    }

    void maxscaleSetTransaction(Parse* pParse, int scope, int access_mode)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_SESSION_WRITE;
        m_operation = mxs::sql::OP_SET_TRANSACTION;

        if (scope == TK_GLOBAL)
        {
            m_type_mask |= mxs::sql::TYPE_GSYSVAR_WRITE;
        }
        else
        {
            if (access_mode == TK_WRITE)
            {
                m_type_mask |= mxs::sql::TYPE_READWRITE;
            }
            else if (access_mode == TK_READ)
            {
                m_type_mask |= mxs::sql::TYPE_READONLY;
            }

            if (scope != TK_SESSION)
            {
                // The SET TRANSACTION affects only the next transaction
                m_type_mask |= mxs::sql::TYPE_NEXT_TRX;
            }
        }
    }

    void maxscaleShow(Parse* pParse, MxsShow* pShow)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_operation = mxs::sql::OP_SHOW;

        switch (pShow->what)
        {
        case MXS_SHOW_COLUMNS:
            {
                m_type_mask = mxs::sql::TYPE_READ;
                const char* zDatabase = nullptr;
                size_t nDatabase = 0;

                if (pShow->pDatabase)
                {
                    zDatabase = pShow->pDatabase->z;
                    nDatabase = pShow->pDatabase->n;

                    update_database_names(zDatabase, nDatabase);
                }

                update_table_names(zDatabase, nDatabase, pShow->pName->z, pShow->pName->n);
            }
            break;

        case MXS_SHOW_CREATE_SEQUENCE:
            m_type_mask = mxs::sql::TYPE_READ;
            break;

        case MXS_SHOW_CREATE_VIEW:
            m_type_mask = mxs::sql::TYPE_READ;
            break;

        case MXS_SHOW_CREATE_TABLE:
            m_type_mask = mxs::sql::TYPE_READ;
            break;

        case MXS_SHOW_DATABASES:
            m_type_mask = mxs::sql::TYPE_SHOW_DATABASES;
            break;

        case MXS_SHOW_INDEX:
        case MXS_SHOW_INDEXES:
        case MXS_SHOW_KEYS:
            m_type_mask = mxs::sql::TYPE_WRITE;
            break;

        case MXS_SHOW_TABLE_STATUS:
            m_type_mask = mxs::sql::TYPE_WRITE;
            break;

        case MXS_SHOW_STATUS:
            switch (pShow->data)
            {
            case MXS_SHOW_VARIABLES_GLOBAL:
            case MXS_SHOW_VARIABLES_SESSION:
            case MXS_SHOW_VARIABLES_UNSPECIFIED:
                m_type_mask = mxs::sql::TYPE_READ;
                break;

            case MXS_SHOW_STATUS_MASTER:
                m_type_mask = mxs::sql::TYPE_WRITE;
                break;

            case MXS_SHOW_STATUS_SLAVE:
                m_type_mask = mxs::sql::TYPE_READ;
                break;

            case MXS_SHOW_STATUS_ALL_SLAVES:
                m_type_mask = mxs::sql::TYPE_READ;
                break;

            default:
                m_type_mask = mxs::sql::TYPE_READ;
                break;
            }
            break;

        case MXS_SHOW_TABLES:
            m_type_mask = mxs::sql::TYPE_SHOW_TABLES;
            if (pShow->pDatabase->z)
            {
                update_database_names(pShow->pDatabase->z, pShow->pDatabase->n);
            }
            break;

        case MXS_SHOW_VARIABLES:
            if (pShow->data == MXS_SHOW_VARIABLES_GLOBAL)
            {
                m_type_mask = mxs::sql::TYPE_GSYSVAR_READ;
            }
            else
            {
                m_type_mask = mxs::sql::TYPE_SYSVAR_READ;
            }
            break;

        case MXS_SHOW_WARNINGS:
            // qc_mysqliembedded claims this.
            m_type_mask = mxs::sql::TYPE_WRITE;
            break;

        default:
            mxb_assert(!true);
        }
    }

    void maxscaleTruncate(Parse* pParse, Token* pDatabase, Token* pName)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_WRITE;
        m_operation = mxs::sql::OP_TRUNCATE;

        char* zDatabase;

        char database[pDatabase ? pDatabase->n + 1 : 0];
        if (pDatabase)
        {
            memcpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;
            zDatabase = database;
        }
        else
        {
            zDatabase = NULL;
        }

        char name[pName->n + 1];
        memcpy(name, pName->z, pName->n);
        name[pName->n] = 0;

        update_names(zDatabase, name, NULL, NULL);
    }

    void maxscaleUse(Parse* pParse, Token* pToken)
    {
        mxb_assert(this_thread.initialized);

        m_status = Parser::Result::PARSED;
        m_type_mask = mxs::sql::TYPE_SESSION_WRITE;
        m_operation = mxs::sql::OP_CHANGE_DB;

        if (should_collect(Parser::COLLECT_DATABASES))
        {
            char copy[pToken->n + 1];
            strncpy(copy, pToken->z, pToken->n);
            copy[pToken->n] = 0;

            exposed_sqlite3Dequote(copy);

            m_database_names.push_back(get_string_view("database", copy));
        }
    }

    void set_type_mask(uint32_t type_mask)
    {
        mxb_assert(this_thread.initialized);
        m_type_mask = type_mask;
    }

    QcSqliteInfo(uint32_t cllct)
        : m_size(0)
        , m_status(Parser::Result::INVALID)
        , m_status_cap(Parser::Result::PARSED)
        , m_collect(cllct)
        , m_collected(0)
        , m_sql_mode(this_thread.sql_mode)
        , m_pFunction_name_mappings(this_thread.pFunction_name_mappings)
        , m_keyword_1(0) // Sqlite3 starts numbering tokens from 1, so 0 means
        , m_keyword_2(0) // that we have not seen a keyword.
        , m_pQuery(NULL)
        , m_nQuery(0)
        , m_type_mask(mxs::sql::TYPE_UNKNOWN)
        , m_operation(mxs::sql::OP_UNDEFINED)
        , m_pPreparable_stmt(NULL)
    {
    }

    ~QcSqliteInfo()
    {
        gwbuf_free(m_pPreparable_stmt);
    }

private:
    bool should_collect(Parser::Collect collect) const
    {
        return (m_collect & collect) && !(m_collected & collect);
    }

    static void free_field_infos(Parser::FieldInfo* pInfos, size_t nInfos)
    {
        MXB_FREE(pInfos);
    }

    static void free_string_array(char** pzArray)
    {
        if (pzArray)
        {
            char** pz = pzArray;

            while (*pz)
            {
                free(*pz);
                ++pz;
            }

            free(pzArray);
        }
    }

    static char** copy_string_array(const vector<char*>& strings)
    {
        size_t n = strings.size();

        char** pz = (char**) MXB_MALLOC((n + 1) * sizeof(char*));
        MXB_ABORT_IF_NULL(pz);

        pz[n] = 0;

        for (size_t i = 0; i < n; ++i)
        {
            pz[i] = MXB_STRDUP(strings[i]);
            MXB_ABORT_IF_NULL(pz[i]);
        }

        return pz;
    }

    Parser::TableName table_name_collected(string_view database, string_view table)
    {
        Parser::TableName needle(database, table);

        auto it = std::find(m_table_names.begin(), m_table_names.end(), needle);

        return it != m_table_names.end() ? *it : Parser::TableName {};
    }

    string_view database_name_collected(const char* zDatabase, size_t nDatabase)
    {
        string_view database(zDatabase, nDatabase);

        auto it = std::find(m_database_names.begin(), m_database_names.end(), database);

        return it != m_database_names.end() ? *it : string_view {};
    }

    string_view update_table_names(const char* zDatabase,
                                   size_t nDatabase,
                                   const char* zTable,
                                   size_t nTable)
    {
        mxb_assert(zTable && nTable);

        string_view database = (nDatabase != 0 ? string_view(zDatabase, nDatabase) : string_view {});
        string_view table(zTable, nTable);

        Parser::TableName collected_name = table_name_collected(database, table);

        if (collected_name.empty())
        {
            if (!database.empty())
            {
                collected_name.db = get_string_view("database", string(database).c_str());
            }

            collected_name.table = get_string_view("table", string(table).c_str());

            m_table_names.push_back(collected_name);
        }

        return collected_name.table;
    }

    string_view update_database_names(const char* zDatabase, size_t nDatabase)
    {
        mxb_assert(zDatabase && nDatabase);

        string_view collected_database = database_name_collected(zDatabase, nDatabase);

        if (collected_database.empty())
        {
            collected_database = get_string_view("database", string(zDatabase, nDatabase).c_str());

            m_database_names.push_back(collected_database);
        }

        return collected_database;
    }

    string_view get_string_view(const char* zContext, const char* zNeedle)
    {
        string_view rv;

        const char* pMatch = nullptr;
        size_t n = strlen(zNeedle);

        auto i = m_canonical.find(zNeedle);

        if (i != string::npos)
        {
            pMatch = &m_canonical[i];
        }
        else
        {
            // Ok, let's try case-insensitively.
            pMatch = strcasestr(const_cast<char*>(m_canonical.c_str()), zNeedle);

            if (!pMatch)
            {
                complain_about_missing(zContext, zNeedle);

                string_view needle(zNeedle);

                for (const auto& scratch_buffer : m_scratch_buffers)
                {
                    if (sv_case_eq(string_view(scratch_buffer.data(), scratch_buffer.size()), needle))
                    {
                        pMatch = scratch_buffer.data();
                        break;
                    }
                }

                if (!pMatch)
                {
                    m_scratch_buffers.emplace_back(needle.begin(), needle.end());

                    const auto& scratch_buffer = m_scratch_buffers.back();

                    pMatch = scratch_buffer.data();
                }
            }
        }

        rv = string_view(pMatch, n);

        return rv;
    }

    void populate_field_info(Parser::FieldInfo& info,
                             const char* zDatabase,
                             const char* zTable,
                             const char* zColumn)
    {
        if (zDatabase)
        {
            info.database = get_string_view("database", zDatabase);
        }

        if (zTable)
        {
            info.table = get_string_view("table", zTable);
        }

        mxb_assert(zColumn);
        info.column = get_string_view("column", zColumn);
    }

    void complain_about_missing(const char* zWhat, const char* zKey)
    {
        // As a failure to find a symbol in the canonical statement is not necessarily
        // an indication of a canonicalization bug, unconditional logging can't really
        // be done. In debug we log a warning so that it is possible to become aware
        // of problems.
#if defined(SS_DEBUG)
        MXB_WARNING("The %s '%s' is not found in the canonical statement '%s' created from "
                    "the statement '%.*s'.",
                    zWhat, zKey, m_canonical.c_str(), (int)m_nQuery, m_pQuery);
#endif
    }


public:
    // TODO: Make these private once everything's been updated.
    mutable int32_t                   m_size;                    // The total amount of memory used.
    int32_t                           m_refs;                    // The reference count.
    Parser::Result                    m_status;                  // The validity of the information.
    Parser::Result                    m_status_cap;              // The cap on '    m_status'.
    uint32_t                          m_collect;                 // What information should be collected.
    uint32_t                          m_collected;               // What information has been collected.
    Parser::SqlMode                   m_sql_mode;                // The current sql_mode.
    QC_NAME_MAPPING*                  m_pFunction_name_mappings; // How function names should be mapped.
    int                               m_keyword_1;               // The first encountered keyword.
    int                               m_keyword_2;               // The second encountered keyword.
    const char*                       m_pQuery;                  // The query passed to sqlite.
    size_t                            m_nQuery;                  // The length of the query.
    uint32_t                          m_type_mask;               // The type mask of the query.
    mxs::sql::OpCode                  m_operation;               // The operation in question.
    string_view                       m_created_table_name;      // The name of a created table.
    string_view                       m_prepare_name;            // The name of a prepared statement.
    GWBUF*                            m_pPreparable_stmt;        // The preparable statement.
    Parser::KillInfo                  m_kill;
    string                            m_canonical;               // The canonical version of the statement.
    vector<string_view>               m_database_names;          // Vector of database names used in the query.
    vector<Parser::TableName>         m_table_names;             // Vector of table names used in the query.
    vector<Parser::FieldInfo>         m_field_infos;             // Vector of fields used by the statement.
    vector<Parser::FunctionInfo>      m_function_infos;          // Vector of functions used by the statement.
    vector<vector<Parser::FieldInfo>> m_function_field_usage;    // Vector of vector fields used by functions
                                                                 // of the statement. Data referred to from
                                                                 // m_function_infos
    vector<vector<char>>              m_scratch_buffers;         // Buffers if string not found from canonical.
};

extern "C"
{

extern void mxs_sqlite3AlterFinishAddColumn(Parse*, Token*);
extern void mxs_sqlite3AlterBeginAddColumn(Parse*, SrcList*);
extern void mxs_sqlite3Analyze(Parse*, SrcList*);
extern void mxs_sqlite3BeginTransaction(Parse*, int token, int type);
extern void mxs_sqlite3CommitTransaction(Parse*);
extern void mxs_sqlite3CreateIndex(Parse*,
                                   Token*,
                                   Token*,
                                   SrcList*,
                                   ExprList*,
                                   int,
                                   Token*,
                                   Expr*,
                                   int,
                                   int);
extern void mxs_sqlite3BeginTrigger(Parse*,
                                    Token*,
                                    Token*,
                                    int,
                                    int,
                                    IdList*,
                                    SrcList*,
                                    Expr*,
                                    int,
                                    int);
extern void mxs_sqlite3FinishTrigger(Parse*, TriggerStep*, Token*);
extern void mxs_sqlite3CreateView(Parse*, Token*, Token*, Token*, ExprList*, Select*, int, int);
extern void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing);
extern void mxs_sqlite3DropIndex(Parse*, SrcList*, SrcList*, int);
extern void mxs_sqlite3DropTable(Parse*, SrcList*, int, int, int);
extern void mxs_sqlite3EndTable(Parse*, Token*, Token*, u8, Select*, SrcList*);
extern void mxs_sqlite3Insert(Parse*, SrcList*, Select*, IdList*, int, ExprList*);
extern void mxs_sqlite3RollbackTransaction(Parse*);
extern void mxs_sqlite3Savepoint(Parse* pParse, int op, Token* pName);
extern int  mxs_sqlite3Select(Parse*, Select*, SelectDest*);
extern void mxs_sqlite3StartTable(Parse*, Token*, Token*, int, int, int, int);
extern void mxs_sqlite3Update(Parse*, SrcList*, ExprList*, Expr*, int);

extern void maxscaleCollectInfoFromSelect(Parse*, Select*, int);

extern void maxscaleAlterTable(Parse*, mxs_alter_t command, SrcList*, Token*);
extern void maxscaleCall(Parse*, SrcList* pName, ExprList* pExprList);
extern void maxscaleCheckTable(Parse*, SrcList* pTables);
extern void maxscaleCreateSequence(Parse*, Token* pDatabase, Token* pTable);
extern void maxscaleDeclare(Parse* pParse);
extern void maxscaleDeallocate(Parse*, Token* pName);
extern void maxscaleDo(Parse*, ExprList* pEList);
extern void maxscaleDrop(Parse*, int what, Token* pDatabase, Token* pName);
extern void maxscaleExecute(Parse*, Token* pName, int type_mask);
extern void maxscaleExecuteImmediate(Parse*, Token* pName, ExprSpan* pExprSpan, int type_mask);
extern void maxscaleExplainTable(Parse*, SrcList* pList);
extern void maxscaleExplain(Parse*);
extern void maxscaleFlush(Parse*, Token* pWhat);
extern void maxscaleHandler(Parse*, mxs_handler_t, SrcList* pFullName, Token* pName);
extern void maxscaleLoadData(Parse*, SrcList* pFullName, int local);
extern void maxscaleLock(Parse*, mxs_lock_t, SrcList*);
extern void maxscaleOptimize(Parse* pParse, SrcList*);
extern void maxscaleOracleAssign(Parse*, Token* pVariable, Expr* pValue);
extern void maxscaleKill(Parse* pParse, MxsKill* pKill);
extern void maxscalePrepare(Parse*, Token* pName, Expr* pStmt);
extern void maxscalePrivileges(Parse*, int kind);
extern void maxscaleRenameTable(Parse*, SrcList* pTables);
extern void maxscaleReset(Parse*, int what);
extern void maxscaleSet(Parse*, int scope, mxs_set_t kind, ExprList*);
extern void maxscaleSetPassword(Parse*);
extern void maxscaleSetTransaction(Parse*, int scope, int access_mode);
extern void maxscaleSetVariable(Parse* pParse, int scope, Expr* pExpr);
extern void maxscaleShow(Parse*, MxsShow* pShow);
extern void maxscaleTruncate(Parse*, Token* pDatabase, Token* pName);
extern void maxscaleUse(Parse*, Token*);

extern void maxscale_update_function_info(const char* name, const Expr* pExpr);
// 'unsigned int' and not 'uint32_t' because 'uint32_t' is unknown in sqlite3 context.
extern void maxscale_set_type_mask(unsigned int type_mask);

extern int  maxscaleComment();
extern int  maxscaleKeyword(int token);
extern void maxscaleSetStatusCap(int cap); // See sqlite3:tokenize.c
extern int  maxscaleTranslateKeyword(int token);
}

static bool ensure_query_is_parsed(const mxs::Parser::Helper& helper, const GWBUF* query, uint32_t collect)
{
    bool parsed = query_is_parsed(query, collect);

    if (!parsed)
    {
        parsed = parse_query(helper, *query, collect);
    }

    return parsed;
}

static void parse_query_string(const char* query, int len, bool suppress_logging)
{
    sqlite3_stmt* stmt = NULL;
    const char* tail = NULL;

    mxb_assert(this_thread.pDb);
    int rc = sqlite3_prepare(this_thread.pDb, query, len, &stmt, &tail);

    const int max_len = 512;    // Maximum length of logged statement.
    const int l = (len > max_len ? max_len : len);
    const char* suffix = (len > max_len ? "..." : "");
    const char* format;

    if (this_thread.pInfo->m_status > this_thread.pInfo->m_status_cap)
    {
        this_thread.pInfo->m_status = this_thread.pInfo->m_status_cap;
    }

    if (this_thread.pInfo->m_operation == mxs::sql::OP_EXPLAIN)
    {
        this_thread.pInfo->m_status = Parser::Result::PARSED;
    }

    if (rc != SQLITE_OK)
    {
        if (qc_info_was_tokenized(this_thread.pInfo->m_status))
        {
            format =
                "Statement was classified only based on keywords "
                "(Sqlite3 error: %s, %s): \"%.*s%s\"";
        }
        else
        {
            if (qc_info_was_parsed(this_thread.pInfo->m_status))
            {
                format =
                    "Statement was only partially parsed "
                    "(Sqlite3 error: %s, %s): \"%.*s%s\"";

                // The status was set to Parser::Result::PARSED, but sqlite3 returned an
                // error. Most likely, query contains some excess unrecognized stuff.
                this_thread.pInfo->m_status = Parser::Result::PARTIALLY_PARSED;
            }
            else
            {
                format =
                    "Statement was neither parsed nor recognized from keywords "
                    "(Sqlite3 error: %s, %s): \"%.*s%s\"";
            }
        }

        if (!suppress_logging)
        {
            if (this_unit.log_level > QC_LOG_NOTHING)
            {
                bool log_warning = false;

                switch (this_unit.log_level)
                {
                case QC_LOG_NON_PARSED:
                    log_warning = this_thread.pInfo->m_status < Parser::Result::PARSED;
                    break;

                case QC_LOG_NON_PARTIALLY_PARSED:
                    log_warning = this_thread.pInfo->m_status < Parser::Result::PARTIALLY_PARSED;
                    break;

                case QC_LOG_NON_TOKENIZED:
                    log_warning = this_thread.pInfo->m_status < Parser::Result::TOKENIZED;
                    break;

                default:
                    mxb_assert(!true);
                    break;
                }

                if (log_warning)
                {
                    MXB_WARNING(format,
                                sqlite3_errstr(rc),
                                sqlite3_errmsg(this_thread.pDb),
                                l,
                                query,
                                suffix);
                }
            }
        }
    }
    else if (this_thread.initialized)   // If we are initializing, the query will not be classified.
    {
        if (!suppress_logging && (this_unit.log_level > QC_LOG_NOTHING))
        {
            if (qc_info_was_tokenized(this_thread.pInfo->m_status))
            {
                // This suggests a callback from the parser into this module is not made.
                format =
                    "Statement was classified only based on keywords, "
                    "even though the statement was parsed: \"%.*s%s\"";

                MXB_WARNING(format, l, query, suffix);
            }
            else if (!qc_info_was_parsed(this_thread.pInfo->m_status))
            {
                // This suggests there are keywords that should be recognized but are not,
                // a tentative classification cannot be (or is not) made using the keywords
                // seen and/or a callback from the parser into this module is not made.
                format = "Statement was parsed, but not classified: \"%.*s%s\"";

                MXB_WARNING(format, l, query, suffix);
            }
        }
    }

    if (stmt)
    {
        sqlite3_finalize(stmt);
    }
}

static bool parse_query(const mxs::Parser::Helper& helper, const GWBUF& query, uint32_t collect)
{
    bool parsed = false;
    mxb_assert(!query_is_parsed(&query, collect));

    std::string_view sql = helper.get_sql(query);

    if (!sql.empty())
    {
        bool suppress_logging = false;
        bool is_prepare = helper.is_prepare(query);

        auto* pInfo = static_cast<QcSqliteInfo*>(query.get_protocol_info().get());
        if (pInfo)
        {
            mxb_assert((~pInfo->m_collect & collect) != 0);
            mxb_assert((~pInfo->m_collected & collect) != 0);

            // If we get here, then the statement has been parsed once, but
            // not all needed was collected. Now we turn on all blinkenlichts to
            // ensure that a statement is parsed at most twice.
            pInfo->m_collect = Parser::COLLECT_ALL;

            // We also reset the collected keywords, so that code that behaves
            // differently depending on whether keywords have been seem or not
            // acts the same way on this second round.
            pInfo->m_keyword_1 = 0;
            pInfo->m_keyword_2 = 0;

            // And turn off logging. Any parsing issues were logged on the first round.
            suppress_logging = true;
        }
        else
        {
            auto sInfo = QcSqliteInfo::create(collect);
            pInfo = sInfo.get();
            if (pInfo)
            {
                const_cast<GWBUF&>(query).set_protocol_info(std::move(sInfo));

                pInfo->m_canonical = helper.get_sql(query);
                maxsimd::get_canonical(&pInfo->m_canonical, &this_thread.markers);

                if (is_prepare)
                {
                    // This is to ensure that a COM_QUERY and a COM_STMT_PREPARE
                    // containing the same statement have a different canonical string.
                    pInfo->m_canonical.append(":P");
                }

                pInfo->m_canonical.shrink_to_fit();
            }
        }

        if (pInfo)
        {
            this_thread.pInfo = pInfo;
            this_thread.pHelper = &helper;

            const char* s = sql.data();
            size_t len = sql.length();

            this_thread.pInfo->m_pQuery = s;
            this_thread.pInfo->m_nQuery = len;
            parse_query_string(s, len, suppress_logging);
            this_thread.pInfo->m_pQuery = nullptr;
            this_thread.pInfo->m_nQuery = 0;

            if (is_prepare)
            {
                pInfo->m_type_mask |= mxs::sql::TYPE_PREPARE_STMT;
            }

            pInfo->m_collected = pInfo->m_collect;

            pInfo->calculate_size();

            parsed = true;

            this_thread.pHelper = nullptr;
            this_thread.pInfo = nullptr;
        }
        else
        {
            MXB_ERROR("Could not allocate structure for containing parse data.");
        }
    }
    else
    {
        // TODO: It would be better if the parser was asked to parse a buffer
        // TODO: only when it is known to contain something parsable.
        MXB_WARNING("The provided buffer does not contain SQL.");
        // mxb_assert(!true);
    }

    return parsed;
}

static bool query_is_parsed(const GWBUF* pQuery, uint32_t collect)
{
    bool rc = false;

    if (pQuery)
    {
        auto* pInfo = static_cast<QcSqliteInfo*>(pQuery->get_protocol_info().get());

        if (pInfo)
        {
            if ((~pInfo->m_collected & collect) != 0)
            {
                // The statement has been parsed once, but the needed information
                // was not collected at that time.
            }
            else
            {
                rc = true;
            }
        }
    }

    return rc;
}

/**
 * Map a function name to another.
 *
 * @param function_name_mappings  The name mapping to use.
 * @param from                    The function name to map.
 *
 * @param The mapped name, or @c from if the name is not mapped.
 */
static const char* map_function_name(QC_NAME_MAPPING* function_name_mappings, const char* from)
{
    QC_NAME_MAPPING* map = function_name_mappings;
    const char* to = NULL;

    while (!to && map->from)
    {
        if (strcasecmp(from, map->from) == 0)
        {
            to = map->to;
        }
        else
        {
            ++map;
        }
    }

    return to ? to : from;
}

static bool should_exclude(const char* zName, const ExprList* pExclude)
{
    int i;
    for (i = 0; i < pExclude->nExpr; ++i)
    {
        const ExprList::ExprList_item* item = &pExclude->a[i];

        // zName will contain a possible alias name. If the alias name
        // is referred to in e.g. in a having, it need to be excluded
        // from the affected fields. It's not a real field.
        if (item->zName && (strcasecmp(item->zName, zName) == 0))
        {
            break;
        }

        Expr* pExpr = item->pExpr;

        if (pExpr->op == TK_EQ)
        {
            // We end up here e.g with "UPDATE t set t.col = 5 ..."
            // So, we pick the left branch.
            pExpr = pExpr->pLeft;
        }

        while (pExpr->op == TK_DOT)
        {
            pExpr = pExpr->pRight;
        }

        if (pExpr->op == TK_ID)
        {
            // We need to ensure that we do not report fields where there
            // is only a difference in case. E.g.
            //     SELECT A FROM tbl WHERE a = "foo";
            // Affected fields is "A" and not "A a".
            if (strcasecmp(pExpr->u.zToken, zName) == 0)
            {
                break;
            }
        }
    }

    return i != pExclude->nExpr;
}

extern void maxscale_update_function_info(const char* name, const Expr* pExpr)
{
    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    pInfo->update_function_info(NULL, name, pExpr, NULL);
}

extern void maxscale_set_type_mask(unsigned int type_mask)
{
    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    pInfo->set_type_mask(type_mask);
}

static const char* get_token_symbol(int token)
{
    switch (token)
    {
    case TK_EQ:
        return "=";

    case TK_GE:
        return ">=";

    case TK_GT:
        return ">";

    case TK_LE:
        return "<=";

    case TK_LT:
        return "<";

    case TK_NE:
        return "<>";


    case TK_BETWEEN:
        return "between";

    case TK_BITAND:
        return "&";

    case TK_BITOR:
        return "|";

    case TK_CASE:
        return "case";

    case TK_CAST:
        return "cast";

    case TK_DIV:
        return "div";

    case TK_IN:
        return "in";

    case TK_ISNULL:
        return "isnull";

    case TK_MINUS:
        return "-";

    case TK_MOD:
        return "mod";

    case TK_NOTNULL:
        return "isnotnull";

    case TK_PLUS:
        return "+";

    case TK_REM:
        return "%";

    case TK_SLASH:
        return "/";

    case TK_STAR:
        return "*";

    case TK_UMINUS:
        return "-";

    default:
        mxb_assert(!true);
        return "";
    }
}

/**
 *
 * SQLITE
 *
 * These functions are called from sqlite.
 */

void mxs_sqlite3AlterFinishAddColumn(Parse* pParse, Token* pToken)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3AlterFinishAddColumn(pParse, pToken));
}

void mxs_sqlite3AlterBeginAddColumn(Parse* pParse, SrcList* pSrcList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3AlterBeginAddColumn(pParse, pSrcList));
}

void mxs_sqlite3Analyze(Parse* pParse, SrcList* pSrcList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Analyze(pParse, pSrcList));
}

void mxs_sqlite3BeginTransaction(Parse* pParse, int token, int type)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3BeginTransaction(pParse, token, type));
}

void mxs_sqlite3BeginTrigger(Parse* pParse,         /* The parse context of the CREATE TRIGGER statement */
                             Token* pName1,         /* The name of the trigger */
                             Token* pName2,         /* The name of the trigger */
                             int tr_tm,             /* One of TK_BEFORE, TK_AFTER, TK_INSTEAD */
                             int op,                /* One of TK_INSERT, TK_UPDATE, TK_DELETE */
                             IdList* pColumns,      /* column list if this is an UPDATE OF trigger */
                             SrcList* pTableName,   /* The name of the table/view the trigger applies to */
                             Expr* pWhen,           /* WHEN clause */
                             int isTemp,            /* True if the TEMPORARY keyword is present */
                             int noErr)             /* Suppress errors if the trigger already exists */
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3BeginTrigger(pParse,
                                                      pName1,
                                                      pName2,
                                                      tr_tm,
                                                      op,
                                                      pColumns,
                                                      pTableName,
                                                      pWhen,
                                                      isTemp,
                                                      noErr));
}

void mxs_sqlite3CommitTransaction(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3CommitTransaction(pParse));
}

void mxs_sqlite3CreateIndex(Parse* pParse,      /* All information about this parse */
                            Token* pName1,      /* First part of index name. May be NULL */
                            Token* pName2,      /* Second part of index name. May be NULL */
                            SrcList* pTblName,  /* Table to index. Use pParse->pNewTable if 0 */
                            ExprList* pList,    /* A list of columns to be indexed */
                            int onError,        /* OE_Abort, OE_Ignore, OE_Replace, or OE_None */
                            Token* pStart,      /* The CREATE token that begins this statement */
                            Expr* pPIWhere,     /* WHERE clause for partial indices */
                            int sortOrder,      /* Sort order of primary key when pList==NULL */
                            int ifNotExist)     /* Omit error if index already exists */
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3CreateIndex(pParse,
                                                     pName1,
                                                     pName2,
                                                     pTblName,
                                                     pList,
                                                     onError,
                                                     pStart,
                                                     pPIWhere,
                                                     sortOrder,
                                                     ifNotExist));
}

void mxs_sqlite3CreateView(Parse* pParse,       /* The parsing context */
                           Token* pBegin,       /* The CREATE token that begins the statement */
                           Token* pName1,       /* The token that holds the name of the view */
                           Token* pName2,       /* The token that holds the name of the view */
                           ExprList* pCNames,   /* Optional list of view column names */
                           Select* pSelect,     /* A SELECT statement that will become the new view */
                           int isTemp,          /* TRUE for a TEMPORARY view */
                           int noErr)           /* Suppress error messages if VIEW already exists */
{
    QC_TRACE();
    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3CreateView(pParse,
                                                    pBegin,
                                                    pName1,
                                                    pName2,
                                                    pCNames,
                                                    pSelect,
                                                    isTemp,
                                                    noErr));
}

void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3DeleteFrom(pParse, pTabList, pWhere, pUsing));
}

void mxs_sqlite3DropIndex(Parse* pParse, SrcList* pName, SrcList* pTable, int bits)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3DropIndex(pParse, pName, pTable, bits));
}

void mxs_sqlite3DropTable(Parse* pParse, SrcList* pName, int isView, int noErr, int isTemp)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3DropTable(pParse, pName, isView, noErr, isTemp));
}

void mxs_sqlite3EndTable(Parse* pParse,     /* Parse context */
                         Token* pCons,      /* The ',' token after the last column defn. */
                         Token* pEnd,       /* The ')' before options in the CREATE TABLE */
                         u8 tabOpts,        /* Extra table options. Usually 0. */
                         Select* pSelect,   /* Select from a "CREATE ... AS SELECT" */
                         SrcList* pOldTable)/* The old table in "CREATE ... LIKE OldTable" */
{
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        mxb_assert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3EndTable(pParse, pCons, pEnd, tabOpts, pSelect, pOldTable));
    }
    else
    {
        exposed_sqlite3EndTable(pParse, pCons, pEnd, tabOpts, pSelect);
    }
}

void mxs_sqlite3FinishTrigger(Parse* pParse,            /* Parser context */
                              TriggerStep* pStepList,   /* The triggered program */
                              Token* pAll)              /* Token that describes the complete CREATE TRIGGER */
{
    QC_TRACE();

    exposed_sqlite3FinishTrigger(pParse, pStepList, pAll);
}

void mxs_sqlite3Insert(Parse* pParse,
                       SrcList* pTabList,
                       Select* pSelect,
                       IdList* pColumns,
                       int onError,
                       ExprList* pSet)
{
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        mxb_assert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Insert(pParse, pTabList, pSelect, pColumns, onError, pSet));
    }
    else
    {
        exposed_sqlite3ExprListDelete(pParse->db, pSet);
        exposed_sqlite3Insert(pParse, pTabList, pSelect, pColumns, onError);
    }
}

void mxs_sqlite3RollbackTransaction(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3RollbackTransaction(pParse));
}

int mxs_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest)
{
    int rc = -1;
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        mxb_assert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Select(pParse, p, pDest));
    }
    else
    {
        rc = exposed_sqlite3Select(pParse, p, pDest);
    }

    return rc;
}

void mxs_sqlite3StartTable(Parse* pParse,   /* Parser context */
                           Token* pName1,   /* First part of the name of the table or view */
                           Token* pName2,   /* Second part of the name of the table or view */
                           int isTemp,      /* True if this is a TEMP table */
                           int isView,      /* True if this is a VIEW */
                           int isVirtual,   /* True if this is a VIRTUAL table */
                           int noErr)       /* Do nothing if table already exists */
{
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        mxb_assert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3StartTable(pParse,
                                                        pName1,
                                                        pName2,
                                                        isTemp,
                                                        isView,
                                                        isVirtual,
                                                        noErr));
    }
    else
    {
        exposed_sqlite3StartTable(pParse, pName1, pName2, isTemp, isView, isVirtual, noErr);
    }
}

void mxs_sqlite3Update(Parse* pParse, SrcList* pTabList, ExprList* pChanges, Expr* pWhere, int onError)
{
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        mxb_assert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Update(pParse, pTabList, pChanges, pWhere, onError));
    }
    else
    {
        // NOTE: Basically we should call
        // NOTE:
        // NOTE: exposed_sqlite3Update(pParse, pTabList, pChanges, pWhere, onError);
        // NOTE:
        // NOTE: However, for whatever reason sqlite3 thinks there is some problem.
        // NOTE: As this final update is not needed, we simply ignore it. That's
        // NOTE: what always has been done but now it is explicit.

        exposed_sqlite3SrcListDelete(pParse->db, pTabList);
        exposed_sqlite3ExprListDelete(pParse->db, pChanges);
        exposed_sqlite3ExprDelete(pParse->db, pWhere);
    }
}

void mxs_sqlite3Savepoint(Parse* pParse, int op, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Savepoint(pParse, op, pName));
}

void maxscaleCollectInfoFromSelect(Parse* pParse, Select* pSelect, int sub_select)
{
    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCollectInfoFromSelect(pParse, pSelect, sub_select));
}

void maxscaleAlterTable(Parse* pParse,              /* Parser context. */
                        mxs_alter_t command,
                        SrcList* pSrc,              /* The table to rename. */
                        Token* pName)               /* The new table name (RENAME). */
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleAlterTable(pParse, command, pSrc, pName));
}

void maxscaleCall(Parse* pParse, SrcList* pName, ExprList* pExprList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCall(pParse, pName, pExprList));
}

void maxscaleCheckTable(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCheckTable(pParse, pTables));
}

void maxscaleCreateSequence(Parse* pParse, Token* pDatabase, Token* pTable)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCreateSequence(pParse, pDatabase, pTable));
}

int maxscaleComment()
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    int rc = 0;

    QC_EXCEPTION_GUARD(rc = pInfo->maxscaleComment());

    return rc;
}

void maxscaleDeclare(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDeclare(pParse));
}

void maxscaleDeallocate(Parse* pParse, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDeallocate(pParse, pName));
}

void maxscaleDo(Parse* pParse, ExprList* pEList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDo(pParse, pEList));
}

void maxscaleDrop(Parse* pParse, int what, Token* pDatabase, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDrop(pParse, what, pDatabase, pName));
}

void maxscaleExecute(Parse* pParse, Token* pName, int type_mask)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleExecute(pParse, pName, type_mask));
}

void maxscaleExecuteImmediate(Parse* pParse, Token* pName, ExprSpan* pExprSpan, int type_mask)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleExecuteImmediate(pParse, pName, pExprSpan, type_mask));
}

void maxscaleExplainTable(Parse* pParse, SrcList* pList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleExplainTable(pParse, pList));
}

void maxscaleExplain(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleExplain(pParse));
}

void maxscaleFlush(Parse* pParse, Token* pWhat)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleFlush(pParse, pWhat));
}

void maxscaleHandler(Parse* pParse, mxs_handler_t type, SrcList* pFullName, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleHandler(pParse, type, pFullName, pName));
}

void maxscaleLoadData(Parse* pParse, SrcList* pFullName, int local)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleLoadData(pParse, pFullName, local));
}

void maxscaleOptimize(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleOptimize(pParse, pTables));
}

void maxscaleKill(Parse* pParse, MxsKill* pKill)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleKill(pParse, pKill));
}

void maxscaleLock(Parse* pParse, mxs_lock_t type, SrcList* pTables)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleLock(pParse, type, pTables));
}

void maxscaleSetStatusCap(int cap)
{
    QC_TRACE();

    mxb_assert((cap >= (int)Parser::Result::INVALID) && (cap <= (int)Parser::Result::PARSED));

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleSetStatusCap(static_cast<Parser::Result>(cap)));
}

int maxscaleTranslateKeyword(int token)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(token = pInfo->maxscaleTranslateKeyword(token));

    return token;
}

/**
 * Register the tokenization of a keyword.
 *
 * @param token A keyword code (check generated parse.h)
 *
 * @return Non-zero if all input should be consumed, 0 otherwise.
 */
int maxscaleKeyword(int token)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(token = pInfo->maxscaleKeyword(token));

    return token;
}

void maxscaleRenameTable(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleRenameTable(pParse, pTables));
}

void maxscalePrepare(Parse* pParse, Token* pName, Expr* pStmt)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscalePrepare(pParse, pName, pStmt));
}

void maxscalePrivileges(Parse* pParse, int kind)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscalePrivileges(pParse, kind));
}

void maxscaleReset(Parse* pParse, int what)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleReset(pParse, what));
}

void maxscaleOracleAssign(Parse* pParse, Token* pVariable, Expr* pValue)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleOracleAssign(pParse, pVariable, pValue));
}

void maxscaleSet(Parse* pParse, int scope, mxs_set_t kind, ExprList* pList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleSet(pParse, scope, kind, pList));
}

void maxscaleSetPassword(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleSetPassword(pParse));
}

void maxscaleSetVariable(Parse* pParse, int scope, Expr* pExpr)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleSetVariable(pParse, scope, pExpr));
}

void maxscaleSetTransaction(Parse* pParse, int scope, int access_mode)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleSetTransaction(pParse, scope, access_mode));
}

void maxscaleShow(Parse* pParse, MxsShow* pShow)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleShow(pParse, pShow));
}

void maxscaleTruncate(Parse* pParse, Token* pDatabase, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleTruncate(pParse, pDatabase, pName));
}

void maxscaleUse(Parse* pParse, Token* pToken)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    mxb_assert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleUse(pParse, pToken));
}

/**
 * API
 */
static int32_t qc_sqlite_process_init(void);
static void    qc_sqlite_process_end(void);
static int32_t qc_sqlite_thread_init(void);
static void    qc_sqlite_thread_end(void);


static bool get_key_and_value(char* arg, const char** pkey, const char** pvalue)
{
    char* p = strchr(arg, '=');

    if (p)
    {
        *p = 0;

        *pkey = mxb::trim(arg);
        *pvalue = mxb::trim(p + 1);
    }

    return p != NULL;
}

static const char ARG_LOG_UNRECOGNIZED_STATEMENTS[] = "log_unrecognized_statements";

static int32_t qc_sqlite_process_init(void)
{
    QC_TRACE();
    mxb_assert(!this_unit.initialized);

    if (sqlite3_initialize() == 0)
    {
        init_builtin_functions();

        this_unit.initialized = true;

        if (this_unit.log_level != QC_LOG_NOTHING)
        {
            const char* message = NULL;

            switch (this_unit.log_level)
            {
            case QC_LOG_NON_PARSED:
                message = "Statements that cannot be parsed completely are logged.";
                break;

            case QC_LOG_NON_PARTIALLY_PARSED:
                message = "Statements that cannot even be partially parsed are logged.";
                break;

            case QC_LOG_NON_TOKENIZED:
                message = "Statements that cannot even be classified by keyword matching are logged.";
                break;

            default:
                mxb_assert(!true);
            }

            MXB_NOTICE("%s", message);
        }
    }
    else
    {
        MXB_ERROR("Failed to initialize sqlite3.");
    }

    return this_unit.initialized ? 0 : -1;
}

static void qc_sqlite_process_end(void)
{
    QC_TRACE();
    mxb_assert(this_unit.initialized);

    finish_builtin_functions();

    sqlite3_shutdown();
    this_unit.initialized = false;
}

static int32_t qc_sqlite_thread_init(void)
{
    QC_TRACE();
    mxb_assert(this_unit.initialized);
    mxb_assert(!this_thread.initialized);

    // Thread initialization must be done behind a global lock. SQLite can perform
    // global initialization which has a data race in the page cache code.
    // TODO: Figure out why this happens
    std::lock_guard<std::mutex> guard(this_unit.lock);

    // TODO: It may be sufficient to have a single in-memory database for all threads.
    int rc = sqlite3_open(":memory:", &this_thread.pDb);
    if (rc == SQLITE_OK)
    {
        this_thread.sql_mode = this_unit.sql_mode;
        this_thread.pFunction_name_mappings = this_unit.pFunction_name_mappings;

        MXB_INFO("In-memory sqlite database successfully opened for thread %lu.",
                 (unsigned long) pthread_self());

        std::unique_ptr<QcSqliteInfo> sInfo = QcSqliteInfo::create(Parser::COLLECT_ALL);

        if (sInfo)
        {
            this_thread.pInfo = sInfo.get();

            // With this statement we cause sqlite3 to initialize itself, so that it
            // is not done as part of the actual classification of data.
            const char* s = "CREATE TABLE __maxscale__internal__ (field int UNIQUE)";
            size_t len = strlen(s);

            bool suppress_logging = false;

            this_thread.pInfo->m_pQuery = s;
            this_thread.pInfo->m_nQuery = len;
            parse_query_string(s, len, suppress_logging);
            this_thread.pInfo->m_pQuery = NULL;
            this_thread.pInfo->m_nQuery = 0;
            this_thread.pInfo = NULL;

            this_thread.initialized = true;
            this_thread.version = VERSION_DEFAULT;
            this_thread.version_major = VERSION_MAJOR_DEFAULT;
            this_thread.version_minor = VERSION_MINOR_DEFAULT;
            this_thread.version_patch = VERSION_PATCH_DEFAULT;
        }
        else
        {
            sqlite3_close(this_thread.pDb);
            this_thread.pDb = NULL;
        }
    }
    else
    {
        MXB_ERROR("Failed to open in-memory sqlite database for thread %lu: %d, %s",
                  (unsigned long) pthread_self(),
                  rc,
                  sqlite3_errstr(rc));
    }

    return this_thread.initialized ? 0 : -1;
}

static void qc_sqlite_thread_end(void)
{
    QC_TRACE();
    mxb_assert(this_unit.initialized);
    mxb_assert(this_thread.initialized);

    mxb_assert(this_thread.pDb);
    std::lock_guard<std::mutex> guard(this_unit.lock);
    int rc = sqlite3_close(this_thread.pDb);

    if (rc != SQLITE_OK)
    {
        MXB_WARNING("The closing of the thread specific sqlite database failed: %d, %s",
                    rc,
                    sqlite3_errstr(rc));
    }

    this_thread.pDb = NULL;
    this_thread.initialized = false;
}

namespace
{

class SqliteParser : public Parser
{
public:
    SqliteParser(const Helper* pHelper)
        : m_helper(*pHelper)
    {
    }

    ParserPlugin& plugin() const override;

    const Helper& helper() const override
    {
        return m_helper;
    }

    Result parse(const GWBUF& stmt, uint32_t collect) const override
    {
        Result result = Parser::Result::INVALID;

        if (QcSqliteInfo* pInfo = get_info(stmt, collect))
        {
            result = pInfo->m_status;
        }

        return result;
    }

    std::string_view get_canonical(const GWBUF& stmt) const override
    {
        std::string_view rv;

        if (QcSqliteInfo* pInfo = get_info(stmt))
        {
            rv = pInfo->get_canonical();
        }

        return rv;
    }

    std::string_view get_created_table_name(const GWBUF& stmt) const override
    {
        std::string_view name;

        if (QcSqliteInfo* pInfo = get_info(stmt, Parser::COLLECT_TABLES))
        {
            if (!pInfo->get_created_table_name(&name))
            {
                log_invalid_data(stmt, "cannot report created table");
            }
        }

        return name;
    }

    Parser::DatabaseNames get_database_names(const GWBUF& stmt) const override
    {
        Parser::DatabaseNames names;

        if (QcSqliteInfo* pInfo = get_info(stmt, Parser::COLLECT_DATABASES))
        {
            if (!pInfo->get_database_names(&names))
            {
                log_invalid_data(stmt, "cannot report what databases are accessed");
            }
        }

        return names;
    }

    void get_field_info(const GWBUF& stmt, const Parser::FieldInfo** ppInfos, size_t* pnInfos) const override
    {
        *ppInfos = nullptr;
        *pnInfos = 0;

        if (QcSqliteInfo* pInfo = get_info(stmt, Parser::COLLECT_FIELDS))
        {
            if (!pInfo->get_field_info(ppInfos, pnInfos))
            {
                log_invalid_data(stmt, "cannot report field info");
            }
        }
    }

    void get_function_info(const GWBUF& stmt,
                           const Parser::FunctionInfo** ppInfos,
                           size_t* pnInfos) const override
    {
        *ppInfos = nullptr;
        *pnInfos = 0;

        if (QcSqliteInfo* pInfo = get_info(stmt, Parser::COLLECT_FUNCTIONS))
        {
            if (!pInfo->get_function_info(ppInfos, pnInfos))
            {
                log_invalid_data(stmt, "cannot report function info");
            }
        }
    }

    Parser::KillInfo get_kill_info(const GWBUF& stmt) const override
    {
        Parser::KillInfo kill;

        if (QcSqliteInfo* pInfo = get_info(stmt))
        {
            if (!pInfo->get_kill_info(&kill))
            {
                log_invalid_data(stmt, "cannot report KILL information");
            }
        }

        return kill;
    }

    mxs::sql::OpCode get_operation(const GWBUF& stmt) const override
    {
        mxs::sql::OpCode op = mxs::sql::OP_UNDEFINED;

        if (QcSqliteInfo* pInfo = get_info(stmt))
        {
            if (!pInfo->get_operation(&op))
            {
                log_invalid_data(stmt, "cannot report query operation");
            }
        }

        return op;
    }

    uint32_t get_options() const override
    {
        return this_thread.options;
    }

    GWBUF* get_preparable_stmt(const GWBUF& stmt) const override
    {
        GWBUF* pPreparable_stmt = nullptr;

        if (QcSqliteInfo* pInfo = get_info(stmt))
        {
            if (!pInfo->get_preparable_stmt(&pPreparable_stmt))
            {
                log_invalid_data(stmt, "cannot report preperable statement");
            }
        }

        return pPreparable_stmt;
    }

    std::string_view get_prepare_name(const GWBUF& stmt) const override
    {
        std::string_view name;

        if (QcSqliteInfo* pInfo = get_info(stmt))
        {
            if (!pInfo->get_prepare_name(&name))
            {
                log_invalid_data(stmt, "cannot report the name of a prepared statement");
            }
        }

        return name;
    }

    uint64_t get_server_version() const override
    {
        return this_thread.version;
    }

    Parser::SqlMode get_sql_mode() const override
    {
        return this_thread.sql_mode;
    }

    Parser::TableNames get_table_names(const GWBUF& stmt) const override
    {
        Parser::TableNames names;

        if (QcSqliteInfo* pInfo = get_info(stmt, Parser::COLLECT_TABLES))
        {
            if (!pInfo->get_table_names(&names))
            {
                log_invalid_data(stmt, "cannot report what tables are accessed");
            }
        }

        return names;
    }

    uint32_t get_trx_type_mask(const GWBUF& stmt) const override
    {
        // TODO: This will not work correctly for Postgres.
        maxscale::TrxBoundaryParser parser;
        return parser.type_mask_of(m_helper.get_sql(stmt));
    }

    uint32_t get_type_mask(const GWBUF& stmt) const override
    {
        uint32_t type_mask = 0;

        if (QcSqliteInfo* pInfo = get_info(stmt))
        {
            if (!pInfo->get_type_mask(&type_mask))
            {
                log_invalid_data(stmt, "cannot report query type");
            }
        }

        return type_mask;
    }

    void set_server_version(uint64_t version) override
    {
        uint32_t major = version / 10000;
        uint32_t minor = (version - major * 10000) / 100;
        uint32_t patch = version - major * 10000 - minor * 100;

        this_thread.version = version;
        this_thread.version_major = major;
        this_thread.version_minor = minor;
        this_thread.version_patch = patch;
    }

    void set_sql_mode(Parser::SqlMode sql_mode) override
    {
        switch (sql_mode)
        {
        case Parser::SqlMode::DEFAULT:
            this_thread.sql_mode = sql_mode;
            this_thread.pFunction_name_mappings = function_name_mappings_default;
            break;

        case Parser::SqlMode::ORACLE:
            this_thread.sql_mode = sql_mode;
            this_thread.pFunction_name_mappings = function_name_mappings_oracle;
            break;

        default:
            mxb_assert(!true);
        }
    }

    bool set_options(uint32_t options) override
    {
        bool rv = false;

        if ((options & ~Parser::OPTION_MASK) == 0)
        {
            this_thread.options = options;
            rv = true;
        }
        else
        {
            mxb_assert(!true);
        }

        return rv;
    }

private:
    QcSqliteInfo* get_info(const GWBUF& stmt, uint32_t collect_extra = 0) const
    {
        mxb_assert(this_unit.initialized);
        mxb_assert(this_thread.initialized);

        uint32_t collect = Parser::COLLECT_ESSENTIALS | collect_extra;

        QcSqliteInfo* pInfo = QcSqliteInfo::get(m_helper, &stmt, collect);

        if (!pInfo)
        {
            MXB_ERROR("The query could not be parsed. Either memory could not be "
                      "allocated or there was no SQL to parse.");
        }

        return pInfo;
    }

    void log_invalid_data(const GWBUF& stmt, const char* zMessage) const
    {
        if (mxb_log_should_log(LOG_INFO))
        {
            auto sql = m_helper.get_sql(stmt);

            if (!sql.empty())
            {
                MXB_INFO("Parsing the query failed, %s: %.*s", zMessage, (int)sql.length(), sql.data());
            }
        }
    }

    const Helper& m_helper;
};

class SqliteParserPlugin : public ParserPlugin
{
public:
    bool setup(Parser::SqlMode sql_mode, const char* cargs) override
    {
        QC_TRACE();
        mxb_assert(!this_unit.setup);

        qc_log_level_t log_level = QC_LOG_NOTHING;
        QC_NAME_MAPPING* function_name_mappings = function_name_mappings_default;

        if (cargs)
        {
            char args[strlen(cargs) + 1];
            strcpy(args, cargs);

            char* p1;
            char* token = strtok_r(args, ",", &p1);

            while (token)
            {
                const char* key;
                const char* value;

                if (get_key_and_value(token, &key, &value))
                {
                    if (strcmp(key, ARG_LOG_UNRECOGNIZED_STATEMENTS) == 0)
                    {
                        char* end;

                        long l = strtol(value, &end, 0);

                        if ((*end == 0) && (l >= QC_LOG_NOTHING) && (l <= QC_LOG_NON_TOKENIZED))
                        {
                            log_level = static_cast<qc_log_level_t>(l);
                        }
                        else
                        {
                            MXB_WARNING("'%s' is not a number between %d and %d.",
                                        value,
                                        QC_LOG_NOTHING,
                                        QC_LOG_NON_TOKENIZED);
                        }
                    }
                    else
                    {
                        MXB_WARNING("'%s' is not a recognized argument.", key);
                    }
                }
                else
                {
                    MXB_WARNING("'%s' is not a recognized argument string.", args);
                }

                token = strtok_r(NULL, ",", &p1);
            }
        }

        if (sql_mode == Parser::SqlMode::ORACLE)
        {
            function_name_mappings = function_name_mappings_oracle;
        }

        this_unit.setup = true;
        this_unit.log_level = log_level;
        this_unit.sql_mode = sql_mode;
        this_unit.pFunction_name_mappings = function_name_mappings;

        return this_unit.setup;
    }

    bool thread_init(void) override
    {
        return qc_sqlite_thread_init() == 0;
    }

    void thread_end(void) override
    {
        qc_sqlite_thread_end();
    }

    bool get_current_stmt(const char** ppStmt, size_t* pLen) override
    {
        bool rv = false;

        if (this_thread.pInfo && this_thread.pInfo->m_pQuery && (this_thread.pInfo->m_nQuery != 0))
        {
            *ppStmt = this_thread.pInfo->m_pQuery;
            *pLen = this_thread.pInfo->m_nQuery;
            rv = true;
        }

        return rv;
    }

    Parser::StmtResult get_stmt_result(const GWBUF::ProtocolInfo* pInfo) override
    {
        return static_cast<const QcSqliteInfo*>(pInfo)->get_result();
    }

    std::string_view get_canonical(const GWBUF::ProtocolInfo* pInfo) override
    {
        string_view canonical;

        static_cast<const QcSqliteInfo*>(pInfo)->get_canonical(&canonical);

        return canonical;
    }

    std::unique_ptr<Parser> create_parser(const Parser::Helper* pHelper) const override
    {
        return std::make_unique<SqliteParser>(pHelper);
    }
};

SqliteParserPlugin sqlite3_plugin;

ParserPlugin& SqliteParser::plugin() const
{
    return sqlite3_plugin;
}

}

/**
 * EXPORTS
 */

extern "C"
{

MXS_MODULE* MXS_CREATE_MODULE()
{

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::PARSER,
        mxs::ModuleStatus::GA,
        MXS_PARSER_VERSION,
        "MariaDB SQL parser using sqlite3.",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &sqlite3_plugin,
        qc_sqlite_process_init,
        qc_sqlite_process_end,
        qc_sqlite_thread_init,
        qc_sqlite_thread_end,
    };

    return &info;
}
}
