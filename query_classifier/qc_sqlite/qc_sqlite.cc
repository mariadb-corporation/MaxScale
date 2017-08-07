/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// NOTE: qc_sqlite used to be C. So as to be able to use STL collections,
// NOTE: it has been ported to C++. However, the porting is only partial,
// NOTE: which is the reason why there is a mix of C-style and C++-style
// NOTE: approaches.

#define MXS_MODULE_NAME "qc_sqlite"
#include <sqliteInt.h>

#include <signal.h>
#include <string.h>
#include <new>
#include <string>
#include <map>
#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/platform.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/query_classifier.h>
#include "builtin_functions.h"

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

#define QC_EXCEPTION_GUARD(statement)\
    do { try { statement; }\
    catch (const std::bad_alloc&) { \
        MXS_OOM(); pInfo->m_status = QC_QUERY_INVALID; } \
    catch (const std::exception& x) { \
        MXS_ERROR("Caught standard exception: %s", x.what()); pInfo->m_status = QC_QUERY_INVALID; } \
    catch (...) { \
        MXS_ERROR("Caught unknown exception."); pInfo->m_status = QC_QUERY_INVALID; } } while (false)

static inline bool qc_info_was_tokenized(qc_parse_result_t status)
{
    return status == QC_QUERY_TOKENIZED;
}

static inline bool qc_info_was_parsed(qc_parse_result_t status)
{
    return status == QC_QUERY_PARSED;
}

typedef enum qc_log_level
{
    QC_LOG_NOTHING = 0,
    QC_LOG_NON_PARSED,
    QC_LOG_NON_PARTIALLY_PARSED,
    QC_LOG_NON_TOKENIZED,
} qc_log_level_t;

typedef enum qc_parse_as
{
    QC_PARSE_AS_DEFAULT, // Parse as embedded lib does before 10.3
    QC_PARSE_AS_103      // Parse as embedded lib does in 10.3
} qc_parse_as_t;

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
    { NULL, NULL }
};

static QC_NAME_MAPPING function_name_mappings_103[] =
{
    // NOTE: If something is added here, add it to function_name_mappings_oracle as well.
    { "now", "current_timestamp" },
    { NULL, NULL }
};

static QC_NAME_MAPPING function_name_mappings_oracle[] =
{
    { "now", "current_timestamp" },
    { "nvl", "ifnull" },
    { NULL, NULL }
};

/**
 * Stores alias information. The key in the mapping is the alias name,
 * and an instance of this struct contains the actual table/database.
 *
 * zDatabase and zTable point to memory that belongs to QcSqliteInfo
 * so they can be simply copied in all contexts.
 */

struct QcAliasValue
{
    QcAliasValue(const char* zD, const char* zT)
        : zDatabase(zD)
        , zTable(zT)
    {
    }

    const char* zDatabase;
    const char* zTable;
};

/**
 * The state of qc_sqlite.
 */
static struct
{
    bool initialized;
    bool setup;
    qc_log_level_t log_level;
    qc_sql_mode_t sql_mode;
    qc_parse_as_t parse_as;
    QC_NAME_MAPPING* pFunction_name_mappings;
} this_unit;

/**
 * The qc_sqlite thread-specific state.
 */
class QcSqliteInfo;

static thread_local struct
{
    bool initialized;                        // Whether the thread specific data has been initialized.
    sqlite3* pDb;                            // Thread specific database handle.
    qc_sql_mode_t sql_mode;                  // What sql_mode is used.
    QcSqliteInfo* pInfo;                     // The information for the current statement being classified.
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    QC_NAME_MAPPING* pFunction_name_mappings; // How function names should be mapped.
} this_thread;

/**
 * HELPERS
 */

typedef enum qc_token_position
{
    QC_TOKEN_MIDDLE, // In the middle or irrelevant, e.g.: "=" in "a = b".
    QC_TOKEN_LEFT,   // To the left, e.g.: "a" in "a = b".
    QC_TOKEN_RIGHT,  // To the right, e.g: "b" in "a = b".
} qc_token_position_t;

static void buffer_object_free(void* data);
static void enlarge_string_array(size_t n, size_t len, char*** ppzStrings, size_t* pCapacity);
static bool ensure_query_is_parsed(GWBUF* query, uint32_t collect);
static void log_invalid_data(GWBUF* query, const char* message);
static const char* map_function_name(QC_NAME_MAPPING* function_name_mappings, const char* name);
static bool parse_query(GWBUF* query, uint32_t collect);
static void parse_query_string(const char* query, int len);
static bool query_is_parsed(GWBUF* query, uint32_t collect);
static bool should_exclude(const char* zName, const ExprList* pExclude);
static void update_field_infos_from_expr(QcSqliteInfo* info,
                                         const Expr* pExpr,
                                         uint32_t usage,
                                         const ExprList* pExclude);
static void update_field_infos(QcSqliteInfo* info,
                               int prev_token,
                               const Expr* pExpr,
                               uint32_t usage,
                               qc_token_position_t pos,
                               const ExprList* pExclude);
static void update_field_infos_from_exprlist(QcSqliteInfo* info,
                                             const ExprList* pEList,
                                             uint32_t usage,
                                             const ExprList* pExclude);
static void update_field_infos_from_idlist(QcSqliteInfo* info,
                                           const IdList* pIds,
                                           uint32_t usage,
                                           const ExprList* pExclude);
static void update_field_infos_from_select(QcSqliteInfo* info,
                                           const Select* pSelect,
                                           uint32_t usage,
                                           const ExprList* pExclude);
static void update_field_infos_from_with(QcSqliteInfo* info,
                                          const With* pWith);
static void update_function_info(QcSqliteInfo* info,
                                 const char* name,
                                 uint32_t usage);
static void update_names_from_srclist(QcSqliteInfo* info, const SrcList* pSrc);

// Defined in parse.y
extern "C"
{

extern void exposed_sqlite3ExprDelete(sqlite3 *db, Expr *pExpr);
extern void exposed_sqlite3ExprListDelete(sqlite3 *db, ExprList *pList);
extern void exposed_sqlite3IdListDelete(sqlite3 *db, IdList *pList);
extern void exposed_sqlite3SrcListDelete(sqlite3 *db, SrcList *pList);
extern void exposed_sqlite3SelectDelete(sqlite3 *db, Select *p);

extern void exposed_sqlite3BeginTrigger(Parse *pParse,
                                        Token *pName1,
                                        Token *pName2,
                                        int tr_tm,
                                        int op,
                                        IdList *pColumns,
                                        SrcList *pTableName,
                                        Expr *pWhen,
                                        int isTemp,
                                        int noErr);
extern void exposed_sqlite3FinishTrigger(Parse *pParse,
                                         TriggerStep *pStepList,
                                         Token *pAll);
extern int exposed_sqlite3Dequote(char *z);
extern int exposed_sqlite3EndTable(Parse*, Token*, Token*, u8, Select*);
extern int exposed_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest);
extern void exposed_sqlite3StartTable(Parse *pParse,   /* Parser context */
                                      Token *pName1,   /* First part of the name of the table or view */
                                      Token *pName2,   /* Second part of the name of the table or view */
                                      int isTemp,      /* True if this is a TEMP table */
                                      int isView,      /* True if this is a VIEW */
                                      int isVirtual,   /* True if this is a VIRTUAL table */
                                      int noErr);      /* Do nothing if table already exists */

}

/**
 * Contains information about a particular query.
 */
class QcSqliteInfo
{
    QcSqliteInfo(const QcSqliteInfo&);
    QcSqliteInfo& operator = (const QcSqliteInfo&);

public:
    static QcSqliteInfo* create(uint32_t collect)
    {
        QcSqliteInfo* pInfo = new (std::nothrow) QcSqliteInfo(collect);
        ss_dassert(pInfo);
        return pInfo;
    }

    static QcSqliteInfo* get(GWBUF* pStmt, uint32_t collect)
    {
        QcSqliteInfo* pInfo = NULL;

        if (ensure_query_is_parsed(pStmt, collect))
        {
            pInfo = (QcSqliteInfo*) gwbuf_get_buffer_object_data(pStmt, GWBUF_PARSING_INFO);
            ss_dassert(pInfo);
        }

        return pInfo;
    }

    ~QcSqliteInfo()
    {
        free_string_array(m_pzTable_names);
        free_string_array(m_pzTable_fullnames);
        free(m_zCreated_table_name);
        free_string_array(m_pzDatabase_names);
        free(m_zPrepare_name);
        gwbuf_free(m_pPreparable_stmt);
        free_field_infos(m_pField_infos, m_field_infos_len);
        free_function_infos(m_pFunction_infos, m_function_infos_len);
    }

    bool is_valid() const
    {
        return m_status != QC_QUERY_INVALID;
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

    bool get_operation(int32_t* pOp) const
    {
        bool rv = false;

        if (is_valid())
        {
            *pOp = m_operation;
            rv = true;
        }

        return rv;
    }

    bool get_created_table_name(char** pzCreated_table_name) const
    {
        bool rv = false;

        if (is_valid())
        {
            if (m_zCreated_table_name)
            {
                *pzCreated_table_name = MXS_STRDUP(m_zCreated_table_name);
                MXS_ABORT_IF_NULL(*pzCreated_table_name);
            }
            rv = true;
        }

        return rv;
    }

    bool is_drop_table_query(int32_t* pIs_drop_table)
    {
        bool rv = false;

        if (is_valid())
        {
            *pIs_drop_table = m_is_drop_table;
            rv = true;
        }

        return rv;
    }

    bool get_table_names(int32_t fullnames, char*** ppzTable_names, int32_t* pTable_size) const
    {
        bool rv = false;

        if (is_valid())
        {
            if (fullnames)
            {
                *ppzTable_names = m_pzTable_fullnames;
            }
            else
            {
                *ppzTable_names = m_pzTable_names;
            }

            if (*ppzTable_names)
            {
                *ppzTable_names = copy_string_array(*ppzTable_names, pTable_size);
            }
            else
            {
                *pTable_size = 0;
            }

            rv = true;
        }

        return rv;
    }

    bool query_has_clause(int32_t* pHas_clause) const
    {
        bool rv = false;

        if (is_valid())
        {
            *pHas_clause = m_has_clause;
            rv = true;
        }

        return rv;
    }

    bool get_database_names(char*** ppzDatabase_names, int* pnDatabase_names) const
    {
        bool rv = false;

        if (is_valid())
        {
            if (m_pzDatabase_names)
            {
                *ppzDatabase_names = copy_string_array(m_pzDatabase_names, pnDatabase_names);
            }
            rv = true;
        }

        return rv;
    }

    bool get_prepare_name(char** pzPrepare_name) const
    {
        bool rv = false;

        if (is_valid())
        {
            *pzPrepare_name = NULL;

            if (m_zPrepare_name)
            {
                *pzPrepare_name = MXS_STRDUP(m_zPrepare_name);
                MXS_ABORT_IF_NULL(*pzPrepare_name);
            }

            rv = true;
        }

        return rv;
    }

    bool get_field_info(const QC_FIELD_INFO** ppInfos, uint32_t* pnInfos) const
    {
        bool rv = false;

        if (is_valid())
        {
            *ppInfos = m_pField_infos;
            *pnInfos = m_field_infos_len;

            rv = true;
        }

        return rv;
    }

    bool get_function_info(const QC_FUNCTION_INFO** ppInfos, uint32_t* pnInfos) const
    {
        bool rv = false;

        if (is_valid())
        {
            *ppInfos = m_pFunction_infos;
            *pnInfos = m_function_infos_len;

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

    // PUBLIC for now at least.

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

        if (m_sql_mode == QC_SQL_MODE_ORACLE)
        {
            // In Oracle mode we ignore the pseudocolumns "currval" and "nextval".
            // We also exclude "lastval", the 10.3 equivalent of "currval".
            if ((strcasecmp(zFunc_name, "currval") == 0) ||
                (strcasecmp(zFunc_name, "nextval") == 0) ||
                (strcasecmp(zFunc_name, "lastval") == 0))
            {
                rv = true;
            }
        }

        if (!rv && (this_unit.parse_as == QC_PARSE_AS_103))
        {
            if ((strcasecmp(zFunc_name, "lastval") == 0) ||
                (strcasecmp(zFunc_name, "nextval") == 0))
            {
                rv = true;
            }
        }

        return rv;
    }

    void update_field_info(const char* zDatabase,
                           const char* zTable,
                           const char* zColumn,
                           uint32_t usage,
                           const ExprList* pExclude)
    {
        ss_dassert(zColumn);

        // NOTE: This must be first, so that the type mask is properly updated
        // NOTE: in case zColumn is "currval" etc.
        if (is_sequence_related_field(zDatabase, zTable, zColumn))
        {
            m_type_mask |= QUERY_TYPE_WRITE;
            return;
        }

        if (!(m_collect & QC_COLLECT_FIELDS) || (m_collected & QC_COLLECT_FIELDS))
        {
            // If field information should not be collected, or if field information
            // has already been collected, we just return.
            return;
        }

        if (!zDatabase && zTable)
        {
            Aliases::const_iterator i = m_aliases.find(zTable);

            if (i != m_aliases.end())
            {
                const QcAliasValue& value = i->second;

                zDatabase = value.zDatabase;
                zTable = value.zTable;
            }
        }

        QC_FIELD_INFO item = { (char*)zDatabase, (char*)zTable, (char*)zColumn, usage };

        size_t i;
        for (i = 0; i < m_field_infos_len; ++i)
        {
            QC_FIELD_INFO* pField_info = m_pField_infos + i;

            if (strcasecmp(item.column, pField_info->column) == 0)
            {
                if (!item.table && !pField_info->table)
                {
                    ss_dassert(!item.database && !pField_info->database);
                    break;
                }
                else if (item.table && pField_info->table && (strcmp(item.table, pField_info->table) == 0))
                {
                    if (!item.database && !pField_info->database)
                    {
                        break;
                    }
                    else if (item.database &&
                             pField_info->database &&
                             (strcmp(item.database, pField_info->database) == 0))
                    {
                        break;
                    }
                }
            }
        }

        QC_FIELD_INFO* pField_infos = NULL;

        if (i == m_field_infos_len) // If true, the field was not present already.
        {
            // If only a column is specified, but not a table or database and we
            // have a list of expressions that should be excluded, we check if the column
            // value is present in that list. This is in order to exclude the second "d" in
            // a statement like "select a as d from x where d = 2".
            if (!(zColumn && !zTable && !zDatabase && pExclude && should_exclude(zColumn, pExclude)))
            {
                if (m_field_infos_len < m_field_infos_capacity)
                {
                    pField_infos = m_pField_infos;
                }
                else
                {
                    size_t capacity = m_field_infos_capacity ? 2 * m_field_infos_capacity : 8;
                    pField_infos = (QC_FIELD_INFO*)MXS_REALLOC(m_pField_infos,
                                                               capacity * sizeof(QC_FIELD_INFO));

                    if (pField_infos)
                    {
                        m_pField_infos = pField_infos;
                        m_field_infos_capacity = capacity;
                    }
                }
            }
        }
        else
        {
            m_pField_infos[i].usage |= usage;
        }

        // If field_infos is NULL, then the field was found and has already been noted.
        if (pField_infos)
        {
            item.database = item.database ? MXS_STRDUP(item.database) : NULL;
            item.table = item.table ? MXS_STRDUP(item.table) : NULL;
            ss_dassert(item.column);
            item.column = MXS_STRDUP(item.column);

            // We are happy if we at least could dup the column.

            if (item.column)
            {
                pField_infos[m_field_infos_len++] = item;
            }
        }
    }

    void update_names(const char* zDatabase, const char* zTable, const char* zAlias)
    {
        ss_dassert(zTable);

        bool should_collect_alias = zAlias && should_collect(QC_COLLECT_FIELDS);
        bool should_collect_table = should_collect_alias || should_collect(QC_COLLECT_TABLES);
        bool should_collect_database = zDatabase &&
            (should_collect_alias || should_collect(QC_COLLECT_DATABASES));

        const char* zCollected_database = NULL;
        const char* zCollected_table = NULL;

        size_t nDatabase = zDatabase ? strlen(zDatabase) : 0;
        size_t nTable = zTable ? strlen(zTable) : 0;

        char database[nDatabase + 1];
        char table[nTable + 1];

        if (should_collect_database)
        {
            strcpy(database, zDatabase);
            exposed_sqlite3Dequote(database);
        }

        if (should_collect_table)
        {
            if (strcasecmp(zTable, "DUAL") != 0)
            {
                strcpy(table, zTable);
                exposed_sqlite3Dequote(table);

                zCollected_table = update_table_names(database, nDatabase, table, nTable);
            }
        }

        if (should_collect_database)
        {
            zCollected_database = update_database_names(database);
        }

        if (zCollected_table && zAlias)
        {
            QcAliasValue value(zCollected_database, zCollected_table);

            m_aliases.insert(Aliases::value_type(zAlias, value));
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
                ss_dassert(pExpr->u.zToken);
                {
                    const char* zToken = pExpr->u.zToken;
                    if (zToken[0] == '@')
                    {
                        if (zToken[1] == '@')
                        {
                            type_mask |= QUERY_TYPE_SYSVAR_READ;
                        }
                        else
                        {
                            type_mask |= QUERY_TYPE_USERVAR_READ;
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

    /**
     * Returns some string from an expression.
     *
     * @param pExpr An expression.
     *
     * @return Some string referred to in pExpr.
     */
    static const char* find_one_string(Expr* pExpr)
    {
        const char* z = NULL;

        if (pExpr->op == TK_STRING)
        {
            ss_dassert(pExpr->u.zToken);
            z = pExpr->u.zToken;
        }

        if (!z && pExpr->pLeft)
        {
            z = find_one_string(pExpr->pLeft);
        }

        if (!z && pExpr->pRight)
        {
            z = find_one_string(pExpr->pRight);
        }

        return z;
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

    //
    // sqlite3 callbacks
    //

    void mxs_sqlite3AlterFinishAddColumn(Parse* pParse, Token* pToken)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
        m_operation = QUERY_OP_ALTER;
    }

    void mxs_sqlite3AlterBeginAddColumn(Parse* pParse, SrcList* pSrcList)
    {
        ss_dassert(this_thread.initialized);

        update_names_from_srclist(this, pSrcList);

        exposed_sqlite3SrcListDelete(pParse->db, pSrcList);
    }

    void mxs_sqlite3Analyze(Parse* pParse, SrcList* pSrcList)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

        update_names_from_srclist(this, pSrcList);

        exposed_sqlite3SrcListDelete(pParse->db, pSrcList);
    }

    void mxs_sqlite3BeginTransaction(Parse* pParse, int token, int type)
    {
        ss_dassert(this_thread.initialized);

        if ((m_sql_mode != QC_SQL_MODE_ORACLE) || (token == TK_START))
        {
            m_status = QC_QUERY_PARSED;
            m_type_mask = QUERY_TYPE_BEGIN_TRX | type;
        }
    }

    void mxs_sqlite3BeginTrigger(Parse *pParse,      /* The parse context of the CREATE TRIGGER statement */
                                 Token *pName1,      /* The name of the trigger */
                                 Token *pName2,      /* The name of the trigger */
                                 int tr_tm,          /* One of TK_BEFORE, TK_AFTER, TK_INSTEAD */
                                 int op,             /* One of TK_INSERT, TK_UPDATE, TK_DELETE */
                                 IdList *pColumns,   /* column list if this is an UPDATE OF trigger */
                                 SrcList *pTableName,/* The name of the table/view the trigger applies to */
                                 Expr *pWhen,        /* WHEN clause */
                                 int isTemp,         /* True if the TEMPORARY keyword is present */
                                 int noErr)          /* Suppress errors if the trigger already exists */
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

        if (pTableName)
        {
            for (size_t i = 0; i < pTableName->nAlloc; ++i)
            {
                const SrcList::SrcList_item* pItem = &pTableName->a[i];

                if (pItem->zName)
                {
                    update_names(pItem->zDatabase, pItem->zName, pItem->zAlias);
                }
            }
        }

        // We need to call this, otherwise finish trigger will not be called.
        exposed_sqlite3BeginTrigger(pParse, pName1, pName2, tr_tm, op, pColumns,
                                    pTableName, pWhen, isTemp, noErr);
    }

    void mxs_sqlite3CommitTransaction(Parse* pParse)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_COMMIT;
    }

    void mxs_sqlite3CreateIndex(Parse *pParse,     /* All information about this parse */
                                Token *pName1,     /* First part of index name. May be NULL */
                                Token *pName2,     /* Second part of index name. May be NULL */
                                SrcList *pTblName, /* Table to index. Use pParse->pNewTable if 0 */
                                ExprList *pList,   /* A list of columns to be indexed */
                                int onError,       /* OE_Abort, OE_Ignore, OE_Replace, or OE_None */
                                Token *pStart,     /* The CREATE token that begins this statement */
                                Expr *pPIWhere,    /* WHERE clause for partial indices */
                                int sortOrder,     /* Sort order of primary key when pList==NULL */
                                int ifNotExist)    /* Omit error if index already exists */
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
        m_operation = QUERY_OP_CREATE;

        if (pTblName)
        {
            update_names_from_srclist(this, pTblName);
        }
        else if (pParse->pNewTable)
        {
            update_names(NULL, pParse->pNewTable->zName, NULL);
        }

        exposed_sqlite3ExprDelete(pParse->db, pPIWhere);
        exposed_sqlite3ExprListDelete(pParse->db, pList);
        exposed_sqlite3SrcListDelete(pParse->db, pTblName);
    }

    void mxs_sqlite3CreateView(Parse *pParse,     /* The parsing context */
                               Token *pBegin,     /* The CREATE token that begins the statement */
                               Token *pName1,     /* The token that holds the name of the view */
                               Token *pName2,     /* The token that holds the name of the view */
                               ExprList *pCNames, /* Optional list of view column names */
                               Select *pSelect,   /* A SELECT statement that will become the new view */
                               int isTemp,        /* TRUE for a TEMPORARY view */
                               int noErr)         /* Suppress error messages if VIEW already exists */
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
        m_operation = QUERY_OP_CREATE;

        const Token* pName = pName2->z ? pName2 : pName1;
        const Token* pDatabase = pName2->z ? pName1 : NULL;

        char name[pName->n + 1];
        strncpy(name, pName->z, pName->n);
        name[pName->n] = 0;

        if (pDatabase)
        {
            char database[pDatabase->n + 1];
            strncpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;

            update_names(database, name, NULL);
        }
        else
        {
            update_names(NULL, name, NULL);
        }

        if (pSelect)
        {
            update_field_infos_from_select(this, pSelect, QC_USED_IN_SELECT, NULL);
        }

        exposed_sqlite3ExprListDelete(pParse->db, pCNames);
        // pSelect is deleted in parse.y
    }

    void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;

        if (m_operation != QUERY_OP_EXPLAIN)
        {
            m_type_mask = QUERY_TYPE_WRITE;
            m_operation = QUERY_OP_DELETE;
            m_has_clause = pWhere ? true : false;

            if (pUsing)
            {
                // Walk through the using declaration and update
                // table and database names.
                for (int i = 0; i < pUsing->nSrc; ++i)
                {
                    const SrcList::SrcList_item* pItem = &pUsing->a[i];

                    update_names(pItem->zDatabase, pItem->zName, pItem->zAlias);
                }

                // Walk through the tablenames while excluding alias
                // names from the using declaration.
                for (int i = 0; i < pTabList->nSrc; ++i)
                {
                    const SrcList::SrcList_item* pTable = &pTabList->a[i];
                    ss_dassert(pTable->zName);
                    int j = 0;
                    bool isSame = false;

                    do
                    {
                        SrcList::SrcList_item* pItem = &pUsing->a[j++];

                        if (strcasecmp(pTable->zName, pItem->zName) == 0)
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
                        update_names(pTable->zDatabase, pTable->zName, NULL);
                    }
                }
            }
            else
            {
                update_names_from_srclist(this, pTabList);
            }

            if (pWhere)
            {
                update_field_infos(this, 0, pWhere, QC_USED_IN_WHERE, QC_TOKEN_MIDDLE, 0);
            }
        }

        exposed_sqlite3ExprDelete(pParse->db, pWhere);
        exposed_sqlite3SrcListDelete(pParse->db, pTabList);
        exposed_sqlite3SrcListDelete(pParse->db, pUsing);
    }

    void mxs_sqlite3DropIndex(Parse* pParse, SrcList* pName, SrcList* pTable, int bits)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
        m_operation = QUERY_OP_DROP;

        update_names_from_srclist(this, pTable);

        exposed_sqlite3SrcListDelete(pParse->db, pName);
        exposed_sqlite3SrcListDelete(pParse->db, pTable);
    }

    void mxs_sqlite3DropTable(Parse *pParse, SrcList *pName, int isView, int noErr, int isTemp)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_WRITE;
        if (!isTemp)
        {
            m_type_mask |= QUERY_TYPE_COMMIT;
        }
        m_operation = QUERY_OP_DROP;
        if (!isView)
        {
            m_is_drop_table = true;
        }
        update_names_from_srclist(this, pName);

        exposed_sqlite3SrcListDelete(pParse->db, pName);
    }

    void mxs_sqlite3EndTable(Parse *pParse,    /* Parse context */
                             Token *pCons,     /* The ',' token after the last column defn. */
                             Token *pEnd,      /* The ')' before options in the CREATE TABLE */
                             u8 tabOpts,       /* Extra table options. Usually 0. */
                             Select *pSelect,  /* Select from a "CREATE ... AS SELECT" */
                             SrcList* pOldTable) /* The old table in "CREATE ... LIKE OldTable" */
    {
        ss_dassert(this_thread.initialized);

        if (pSelect)
        {
            update_field_infos_from_select(this, pSelect, QC_USED_IN_SELECT, NULL);
        }
        else if (pOldTable)
        {
            update_names_from_srclist(this, pOldTable);
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
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;

        if (m_operation != QUERY_OP_EXPLAIN)
        {
            m_type_mask = QUERY_TYPE_WRITE;
            m_operation = QUERY_OP_INSERT;
            ss_dassert(pTabList);
            ss_dassert(pTabList->nSrc >= 1);
            update_names_from_srclist(this, pTabList);

            if (pColumns)
            {
                update_field_infos_from_idlist(this, pColumns, 0, NULL);
            }

            if (pSelect)
            {
                uint32_t usage;

                if (pSelect->selFlags & SF_Values) // Synthesized from VALUES clause
                {
                    usage = 0;
                }
                else
                {
                    usage = QC_USED_IN_SELECT;
                }

                update_field_infos_from_select(this, pSelect, usage, NULL);
            }

            if (pSet)
            {
                update_field_infos_from_exprlist(this, pSet, 0, NULL);
            }
        }

        exposed_sqlite3SrcListDelete(pParse->db, pTabList);
        exposed_sqlite3IdListDelete(pParse->db, pColumns);
        exposed_sqlite3ExprListDelete(pParse->db, pSet);
        // pSelect is deleted in parse.y
    }

    void mxs_sqlite3RollbackTransaction(Parse* pParse)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_ROLLBACK;
    }

    void mxs_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;

        if (m_operation != QUERY_OP_EXPLAIN)
        {
            m_operation = QUERY_OP_SELECT;

            maxscaleCollectInfoFromSelect(pParse, p, 0);
        }
        // NOTE: By convention, the select is deleted in parse.y.
    }

    void mxs_sqlite3StartTable(Parse *pParse,   /* Parser context */
                               Token *pName1,   /* First part of the name of the table or view */
                               Token *pName2,   /* Second part of the name of the table or view */
                               int isTemp,      /* True if this is a TEMP table */
                               int isView,      /* True if this is a VIEW */
                               int isVirtual,   /* True if this is a VIRTUAL table */
                               int noErr)       /* Do nothing if table already exists */
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_operation = QUERY_OP_CREATE;
        m_type_mask = QUERY_TYPE_WRITE;

        if (isTemp)
        {
            m_type_mask |= QUERY_TYPE_CREATE_TMP_TABLE;
        }
        else
        {
            m_type_mask |= QUERY_TYPE_COMMIT;
        }

        const Token* pName = pName2->z ? pName2 : pName1;
        const Token* pDatabase = pName2->z ? pName1 : NULL;

        char name[pName->n + 1];
        strncpy(name, pName->z, pName->n);
        name[pName->n] = 0;

        if (pDatabase)
        {
            char database[pDatabase->n + 1];
            strncpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;

            update_names(database, name, NULL);
        }
        else
        {
            update_names(NULL, name, NULL);
        }

        if (m_collect & QC_COLLECT_TABLES)
        {
            // If information is collected in several passes, then we may
            // this information already.
            if (!m_zCreated_table_name)
            {
                m_zCreated_table_name = MXS_STRDUP(m_pzTable_names[0]);
                MXS_ABORT_IF_NULL(m_zCreated_table_name);
            }
            else
            {
                ss_dassert(m_collect != m_collected);
                ss_dassert(strcmp(m_zCreated_table_name, m_pzTable_names[0]) == 0);
            }
        }
    }

    void mxs_sqlite3Update(Parse* pParse, SrcList* pTabList, ExprList* pChanges, Expr* pWhere, int onError)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;

        if (m_operation != QUERY_OP_EXPLAIN)
        {
            m_type_mask = QUERY_TYPE_WRITE;
            m_operation = QUERY_OP_UPDATE;
            update_names_from_srclist(this, pTabList);
            m_has_clause = (pWhere ? true : false);

            if (pChanges)
            {
                for (int i = 0; i < pChanges->nExpr; ++i)
                {
                    ExprList::ExprList_item* pItem = &pChanges->a[i];

                    update_field_infos(this, 0, pItem->pExpr, QC_USED_IN_SET, QC_TOKEN_MIDDLE, NULL);
                }
            }

            if (pWhere)
            {
                update_field_infos(this, 0, pWhere, QC_USED_IN_WHERE, QC_TOKEN_MIDDLE, pChanges);
            }
        }

        exposed_sqlite3SrcListDelete(pParse->db, pTabList);
        exposed_sqlite3ExprListDelete(pParse->db, pChanges);
        exposed_sqlite3ExprDelete(pParse->db, pWhere);
    }

    void mxs_sqlite3Savepoint(Parse *pParse, int op, Token *pName)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_WRITE;
    }

    void maxscaleCollectInfoFromSelect(Parse* pParse, Select* pSelect, int sub_select)
    {
        ss_dassert(this_thread.initialized);

        if (pSelect->pInto)
        {
            // If there's a single variable, then it's a write.
            // mysql embedded considers it a system var write.
            m_type_mask = QUERY_TYPE_GSYSVAR_WRITE;

            // Also INTO {OUTFILE|DUMPFILE} will be typed as QUERY_TYPE_GSYSVAR_WRITE.
        }
        else
        {
            m_type_mask = QUERY_TYPE_READ;
        }

        uint32_t usage = sub_select ? QC_USED_IN_SUBSELECT : QC_USED_IN_SELECT;

        update_field_infos_from_select(this, pSelect, usage, NULL);
    }

    void maxscaleAlterTable(Parse *pParse,            /* Parser context. */
                            mxs_alter_t command,
                            SrcList *pSrc,            /* The table to rename. */
                            Token *pName)             /* The new table name (RENAME). */
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
        m_operation = QUERY_OP_ALTER;

        switch (command)
        {
        case MXS_ALTER_DISABLE_KEYS:
            update_names_from_srclist(this, pSrc);
            break;

        case MXS_ALTER_ENABLE_KEYS:
            update_names_from_srclist(this, pSrc);
            break;

        case MXS_ALTER_RENAME:
            update_names_from_srclist(this, pSrc);
            break;

        default:
            ;
        }

        exposed_sqlite3SrcListDelete(pParse->db, pSrc);
    }

    void maxscaleCall(Parse* pParse, SrcList* pName, ExprList* pExprList)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_WRITE;

        if (pExprList)
        {
            update_field_infos_from_exprlist(this, pExprList, 0, NULL);
        }

        exposed_sqlite3SrcListDelete(pParse->db, pName);
        exposed_sqlite3ExprListDelete(pParse->db, pExprList);
    }

    void maxscaleCheckTable(Parse* pParse, SrcList* pTables)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

        update_names_from_srclist(this, pTables);

        exposed_sqlite3SrcListDelete(pParse->db, pTables);
    }

    void maxscaleCreateSequence(Parse* pParse, Token* pDatabase, Token* pTable)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;

        const char* zDatabase = NULL;
        char database[pDatabase ? pDatabase->n + 1 : 1];

        if (pDatabase)
        {
            strncpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;

            zDatabase = database;
        }

        char table[pTable->n + 1];
        strncpy(table, pTable->z, pTable->n);
        table[pTable->n] = 0;

        update_names(zDatabase, table, NULL);
    }

    void maxscaleComment()
    {
        ss_dassert(this_thread.initialized);

        if (m_status == QC_QUERY_INVALID)
        {
            m_status = QC_QUERY_PARSED;
            m_type_mask = QUERY_TYPE_READ;
        }
    }

    void maxscaleDeclare(Parse* pParse)
    {
        ss_dassert(this_thread.initialized);

        if (m_sql_mode != QC_SQL_MODE_ORACLE)
        {
            m_status = QC_QUERY_INVALID;
        }
    }

    void maxscaleDeallocate(Parse* pParse, Token* pName)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_WRITE;

        // If information is collected in several passes, then we may
        // this information already.
        if (!m_zPrepare_name)
        {
            m_zPrepare_name = (char*)MXS_MALLOC(pName->n + 1);
            if (m_zPrepare_name)
            {
                memcpy(m_zPrepare_name, pName->z, pName->n);
                m_zPrepare_name[pName->n] = 0;
            }
        }
        else
        {
            ss_dassert(m_collect != m_collected);
            ss_dassert(strncmp(m_zPrepare_name, pName->z, pName->n) == 0);
        }
    }

    void maxscaleDo(Parse* pParse, ExprList* pEList)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_READ | QUERY_TYPE_WRITE);

        exposed_sqlite3ExprListDelete(pParse->db, pEList);
    }

    void maxscaleDrop(Parse* pParse, int what, Token* pDatabase, Token* pName)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
        m_operation = QUERY_OP_DROP;

        if (what == MXS_DROP_SEQUENCE)
        {
            const char* zDatabase = NULL;
            char database[pDatabase ? pDatabase->n + 1 : 1];

            if (pDatabase)
            {
                strncpy(database, pDatabase->z, pDatabase->n);
                database[pDatabase->n] = 0;

                zDatabase = database;
            }

            char table[pName->n + 1];
            strncpy(table, pName->z, pName->n);
            table[pName->n] = 0;

            update_names(zDatabase, table, NULL);
        }
    }

    void maxscaleExecute(Parse* pParse, Token* pName, int type_mask)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | type_mask);
        m_operation = QUERY_OP_EXECUTE;

        // If information is collected in several passes, then we may
        // this information already.
        if (!m_zPrepare_name)
        {
            m_zPrepare_name = (char*)MXS_MALLOC(pName->n + 1);
            if (m_zPrepare_name)
            {
                memcpy(m_zPrepare_name, pName->z, pName->n);
                m_zPrepare_name[pName->n] = 0;
            }
        }
        else
        {
            ss_dassert(m_collect != m_collected);
            ss_dassert(strncmp(m_zPrepare_name, pName->z, pName->n) == 0);
        }
    }

    void maxscaleExecuteImmediate(Parse* pParse, Token* pName, ExprSpan* pExprSpan, int type_mask)
    {
        ss_dassert(this_thread.initialized);

        if (m_sql_mode == QC_SQL_MODE_ORACLE)
        {
            // This should be "EXECUTE IMMEDIATE ...", but as "IMMEDIATE" is not
            // checked by the parser we do it here.

            static const char IMMEDIATE[] = "IMMEDIATE";

            if ((pName->n == sizeof(IMMEDIATE) - 1) && (strncasecmp(pName->z, IMMEDIATE, pName->n)) == 0)
            {
                m_status = QC_QUERY_PARSED;
                m_type_mask = (QUERY_TYPE_WRITE | type_mask);
                m_type_mask |= type_check_dynamic_string(pExprSpan->pExpr);
            }
            else
            {
                m_status = QC_QUERY_INVALID;
            }
        }
        else
        {
            m_status = QC_QUERY_INVALID;
        }

        exposed_sqlite3ExprDelete(pParse->db, pExprSpan->pExpr);
    }

    void maxscaleExplain(Parse* pParse, Token* pNext)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_READ;
        m_operation = QUERY_OP_SHOW;

        if (pNext)
        {
            if (pNext->z)
            {
                const char EXTENDED[]   = "EXTENDED";
                const char PARTITIONS[] = "PARTITIONS";
                const char FORMAT[]     = "FORMAT";
                const char FOR[]        = "FOR";

#define MATCHES_KEYWORD(t, k)  ((t->n == sizeof(k) - 1) && (strncasecmp(t->z, k, t->n) == 0))

                if (MATCHES_KEYWORD(pNext, EXTENDED) ||
                    MATCHES_KEYWORD(pNext, PARTITIONS) ||
                    MATCHES_KEYWORD(pNext, FORMAT) ||
                    MATCHES_KEYWORD(pNext, FOR))
                {
                    m_operation = QUERY_OP_EXPLAIN;
                }
            }
        }
    }

    void maxscaleFlush(Parse* pParse, Token* pWhat)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    }

    void maxscaleHandler(Parse* pParse, mxs_handler_t type, SrcList* pFullName, Token* pName)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;

        switch (type)
        {
        case MXS_HANDLER_OPEN:
            {
                m_type_mask = QUERY_TYPE_WRITE;

                ss_dassert(pFullName->nSrc == 1);
                const SrcList::SrcList_item* pItem = &pFullName->a[0];

                update_names(pItem->zDatabase, pItem->zName, pItem->zAlias);
            }
            break;

        case MXS_HANDLER_CLOSE:
            {
                m_type_mask = QUERY_TYPE_WRITE;

                char zName[pName->n + 1];
                strncpy(zName, pName->z, pName->n);
                zName[pName->n] = 0;

                update_names("*any*", zName, NULL);
            }
            break;

        default:
            ss_dassert(!true);
        }

        exposed_sqlite3SrcListDelete(pParse->db, pFullName);
    }

    void maxscaleLoadData(Parse* pParse, SrcList* pFullName)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_WRITE;
        m_operation = QUERY_OP_LOAD;

        if (pFullName)
        {
            update_names_from_srclist(this, pFullName);

            exposed_sqlite3SrcListDelete(pParse->db, pFullName);
        }
    }

    void maxscaleLock(Parse* pParse, mxs_lock_t type, SrcList* pTables)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_WRITE;

        if (pTables)
        {
            update_names_from_srclist(this, pTables);

            exposed_sqlite3SrcListDelete(pParse->db, pTables);
        }
    }

    int maxscaleTranslateKeyword(int token)
    {
        ss_dassert(this_thread.initialized);

        switch (token)
        {
        case TK_CHARSET:
        case TK_DO:
        case TK_HANDLER:
            if (m_sql_mode == QC_SQL_MODE_ORACLE)
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
        ss_dassert(this_thread.initialized);

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
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
                m_operation = QUERY_OP_ALTER;
                break;

            case TK_BEGIN:
            case TK_DECLARE:
            case TK_FOR:
                if (m_sql_mode == QC_SQL_MODE_ORACLE)
                {
                    // The beginning of a BLOCK. We'll assume it is in a single
                    // COM_QUERY packet and hence one GWBUF.
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = QUERY_TYPE_WRITE;
                    // Return non-0 to cause the entire input to be consumed.
                    rv = 1;
                }
                break;

            case TK_CALL:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                break;

            case TK_CREATE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
                m_operation = QUERY_OP_CREATE;
                break;

            case TK_DELETE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                m_operation = QUERY_OP_DELETE;
                break;

            case TK_DESC:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_READ;
                m_operation = QUERY_OP_EXPLAIN;
                break;

            case TK_DROP:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
                m_operation = QUERY_OP_DROP;
                break;

            case TK_EXECUTE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                break;

            case TK_EXPLAIN:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_READ;
                m_operation = QUERY_OP_EXPLAIN;
                break;

            case TK_GRANT:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
                m_operation = QUERY_OP_GRANT;
                break;

            case TK_HANDLER:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                break;

            case TK_INSERT:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                m_operation = QUERY_OP_INSERT;
                break;

            case TK_LOCK:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                break;

            case TK_PREPARE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_PREPARE_NAMED_STMT;
                break;

            case TK_REPLACE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                m_operation = QUERY_OP_INSERT;
                break;

            case TK_REVOKE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
                m_operation = QUERY_OP_REVOKE;
                break;

            case TK_SELECT:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_READ;
                m_operation = QUERY_OP_SELECT;
                break;

            case TK_SET:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_GSYSVAR_WRITE;
                break;

            case TK_SHOW:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_READ;
                m_operation = QUERY_OP_SHOW;
                break;

            case TK_START:
                // Will produce the right info for START SLAVE.
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                break;

            case TK_UNLOCK:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                break;

            case TK_UPDATE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = QUERY_TYPE_WRITE;
                m_operation = QUERY_OP_UPDATE;
                break;

            case TK_TRUNCATE:
                m_status = QC_QUERY_TOKENIZED;
                m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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
            case TK_CHECK:
                if (m_keyword_2 == TK_TABLE)
                {
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
                }
                break;

            case TK_DEALLOCATE:
                if (m_keyword_2 == TK_PREPARE)
                {
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = QUERY_TYPE_SESSION_WRITE;
                }
                break;

            case TK_LOAD:
                if (m_keyword_2 == TK_DATA)
                {
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = QUERY_TYPE_WRITE;
                    m_operation = QUERY_OP_LOAD;
                }
                break;

            case TK_RENAME:
                if (m_keyword_2 == TK_TABLE)
                {
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
                }
                break;

            case TK_START:
                switch (m_keyword_2)
                {
                case TK_TRANSACTION:
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = QUERY_TYPE_BEGIN_TRX;
                    break;

                default:
                    break;
                }
                break;

            case TK_SHOW:
                switch (m_keyword_2)
                {
                case TK_DATABASES_KW:
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = QUERY_TYPE_SHOW_DATABASES;
                    break;

                case TK_TABLES:
                    m_status = QC_QUERY_TOKENIZED;
                    m_type_mask = QUERY_TYPE_SHOW_TABLES;
                    break;

                default:
                    break;
                }
            }
        }

        return rv;
    }

    void maxscaleRenameTable(Parse* pParse, SrcList* pTables)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT;

        for (int i = 0; i < pTables->nSrc; ++i)
        {
            const SrcList::SrcList_item* pItem = &pTables->a[i];

            ss_dassert(pItem->zName);
            ss_dassert(pItem->zAlias);

            update_names(pItem->zDatabase, pItem->zName, NULL);
            update_names(NULL, pItem->zAlias, NULL); // The new name is passed in the alias field.
        }

        exposed_sqlite3SrcListDelete(pParse->db, pTables);
    }

    void maxscalePrepare(Parse* pParse, Token* pName, Expr* pStmt)
    {
        ss_dassert(this_thread.initialized);

        // If the mode is MODE_ORACLE then if expression contains simply a string
        // we can conclude that the statement has been fully parsed, because it will
        // be sensible to parse the preparable statement. Otherwise we mark the
        // statement as having been partially parsed, since the preparable statement
        // will not contain the full statement.
        if (m_sql_mode == QC_SQL_MODE_ORACLE)
        {
            if (pStmt->op == TK_STRING)
            {
                m_status = QC_QUERY_PARSED;
            }
            else
            {
                m_status = QC_QUERY_PARTIALLY_PARSED;
            }
        }
        else
        {
            // If the mode is not MODE_ORACLE, then only a string is acceptable.
            if (pStmt->op == TK_STRING)
            {
                m_status = QC_QUERY_PARSED;
            }
            else
            {
                m_status = QC_QUERY_INVALID;
            }
        }

        m_type_mask = QUERY_TYPE_PREPARE_NAMED_STMT;

        // If information is collected in several passes, then we may
        // this information already.
        if (!m_zPrepare_name)
        {
            m_zPrepare_name = (char*)MXS_MALLOC(pName->n + 1);
            if (m_zPrepare_name)
            {
                memcpy(m_zPrepare_name, pName->z, pName->n);
                m_zPrepare_name[pName->n] = 0;
            }

            // If the expression just contains a string, then zStmt will
            // be that string. Otherwise it will be _some_ string from the
            // expression. In the latter case we've already marked the result
            // to have been partially parsed.
            const char* zStmt = find_one_string(pStmt);

            if (zStmt)
            {
                size_t preparable_stmt_len = zStmt ? strlen(zStmt) : 0;
                size_t payload_len = 1 + preparable_stmt_len;
                size_t packet_len = MYSQL_HEADER_LEN + payload_len;

                m_pPreparable_stmt = gwbuf_alloc(packet_len);

                if (m_pPreparable_stmt)
                {
                    uint8_t* ptr = GWBUF_DATA(m_pPreparable_stmt);
                    // Payload length
                    *ptr++ = payload_len;
                    *ptr++ = (payload_len >> 8);
                    *ptr++ = (payload_len >> 16);
                    // Sequence id
                    *ptr++ = 0x00;
                    // Command
                    *ptr++ = MYSQL_COM_QUERY;

                    memcpy(ptr, zStmt, preparable_stmt_len);
                }
            }
            else
            {
                m_status = QC_QUERY_INVALID;
            }
        }
        else
        {
            ss_dassert(m_collect != m_collected);
            ss_dassert(strncmp(m_zPrepare_name, pName->z, pName->n) == 0);
        }

        exposed_sqlite3ExprDelete(pParse->db, pStmt);
    }

    void maxscalePrivileges(Parse* pParse, int kind)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

        switch (kind)
        {
        case TK_GRANT:
            m_operation = QUERY_OP_GRANT;
            break;

        case TK_REVOKE:
            m_operation = QUERY_OP_REVOKE;
            break;

        default:
            ss_dassert(!true);
        }
    }

    void maxscaleSet(Parse* pParse, int scope, mxs_set_t kind, ExprList* pList)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = 0; // Reset what was set in maxscaleKeyword

        switch (kind)
        {
        case MXS_SET_TRANSACTION:
            if ((scope == TK_GLOBAL) || (scope == TK_SESSION))
            {
                m_type_mask = QUERY_TYPE_GSYSVAR_WRITE;
            }
            else
            {
                ss_dassert(scope == 0);
                m_type_mask = QUERY_TYPE_WRITE;
            }
            break;

        case MXS_SET_VARIABLES:
            {
                for (int i = 0; i < pList->nExpr; ++i)
                {
                    const ExprList::ExprList_item* pItem = &pList->a[i];

                    switch (pItem->pExpr->op)
                    {
                    case TK_CHARACTER:
                    case TK_NAMES:
                        m_type_mask |= QUERY_TYPE_GSYSVAR_WRITE;
                        break;

                    case TK_EQ:
                        {
                            const Expr* pEq = pItem->pExpr;
                            const Expr* pVariable;
                            const Expr* pValue = pEq->pRight;

                            // pEq->pLeft is either TK_DOT, TK_VARIABLE or TK_ID. If it's TK_DOT,
                            // then pEq->pLeft->pLeft is either TK_VARIABLE or TK_ID and pEq->pLeft->pRight
                            // is either TK_DOT, TK_VARIABLE or TK_ID.

                            // Find the left-most part.
                            pVariable = pEq->pLeft;
                            while (pVariable->op == TK_DOT)
                            {
                                pVariable = pVariable->pLeft;
                                ss_dassert(pVariable);
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
                                m_type_mask |= QUERY_TYPE_USERVAR_WRITE;
                            }
                            else
                            {
                                m_type_mask |= QUERY_TYPE_GSYSVAR_WRITE;
                            }

                            // Set pVariable to point to the rightmost part of the name.
                            pVariable = pEq->pLeft;
                            while (pVariable->op == TK_DOT)
                            {
                                pVariable = pVariable->pRight;
                            }

                            ss_dassert((pVariable->op == TK_VARIABLE) || (pVariable->op == TK_ID));

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

                                    switch (enable)
                                    {
                                    case 0:
                                        m_type_mask |= QUERY_TYPE_BEGIN_TRX;
                                        m_type_mask |= QUERY_TYPE_DISABLE_AUTOCOMMIT;
                                        break;

                                    case 1:
                                        m_type_mask |= QUERY_TYPE_ENABLE_AUTOCOMMIT;
                                        m_type_mask |= QUERY_TYPE_COMMIT;
                                        break;

                                    default:
                                        break;
                                    }
                                }
                            }

                            if (pValue->op == TK_SELECT)
                            {
                                update_field_infos_from_select(this, pValue->x.pSelect,
                                                               QC_USED_IN_SUBSELECT, NULL);
                            }
                        }
                        break;

                    default:
                        ss_dassert(!true);
                    }
                }
            }
            break;

        default:
            ss_dassert(!true);
        }

        exposed_sqlite3ExprListDelete(pParse->db, pList);
    }

    void maxscaleShow(Parse* pParse, MxsShow* pShow)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_operation = QUERY_OP_SHOW;

        uint32_t u = QC_USED_IN_SELECT;

        switch (pShow->what)
        {
        case MXS_SHOW_COLUMNS:
            m_type_mask = QUERY_TYPE_READ;
            break;

        case MXS_SHOW_CREATE_SEQUENCE:
            m_type_mask = QUERY_TYPE_READ;
            break;

        case MXS_SHOW_CREATE_VIEW:
            m_type_mask = QUERY_TYPE_READ;
            break;

        case MXS_SHOW_CREATE_TABLE:
            m_type_mask = QUERY_TYPE_READ;
            break;

        case MXS_SHOW_DATABASES:
            m_type_mask = QUERY_TYPE_SHOW_DATABASES;
            break;

        case MXS_SHOW_INDEX:
        case MXS_SHOW_INDEXES:
        case MXS_SHOW_KEYS:
            m_type_mask = QUERY_TYPE_WRITE;
            break;

        case MXS_SHOW_TABLE_STATUS:
            m_type_mask = QUERY_TYPE_WRITE;
            break;

        case MXS_SHOW_STATUS:
            switch (pShow->data)
            {
            case MXS_SHOW_VARIABLES_GLOBAL:
            case MXS_SHOW_VARIABLES_SESSION:
            case MXS_SHOW_VARIABLES_UNSPECIFIED:
                m_type_mask = QUERY_TYPE_READ;
                break;

            case MXS_SHOW_STATUS_MASTER:
                m_type_mask = QUERY_TYPE_WRITE;
                break;

            case MXS_SHOW_STATUS_SLAVE:
                m_type_mask = QUERY_TYPE_READ;
                break;

            case MXS_SHOW_STATUS_ALL_SLAVES:
                m_type_mask = QUERY_TYPE_READ;
                break;

            default:
                m_type_mask = QUERY_TYPE_READ;
                break;
            }
            break;

        case MXS_SHOW_TABLES:
            m_type_mask = QUERY_TYPE_SHOW_TABLES;
            break;

        case MXS_SHOW_VARIABLES:
            if (pShow->data == MXS_SHOW_VARIABLES_GLOBAL)
            {
                m_type_mask = QUERY_TYPE_GSYSVAR_READ;
            }
            else
            {
                m_type_mask = QUERY_TYPE_SYSVAR_READ;
            }
            break;

        case MXS_SHOW_WARNINGS:
            // qc_mysqliembedded claims this.
            m_type_mask = QUERY_TYPE_WRITE;
            break;

        default:
            ss_dassert(!true);
        }
    }

    void maxscaleTruncate(Parse* pParse, Token* pDatabase, Token* pName)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
        m_operation = QUERY_OP_TRUNCATE;

        char* zDatabase;

        char database[pDatabase ? pDatabase->n + 1 : 0];
        if (pDatabase)
        {
            strncpy(database, pDatabase->z, pDatabase->n);
            database[pDatabase->n] = 0;
            zDatabase = database;
        }
        else
        {
            zDatabase = NULL;
        }

        char name[pName->n + 1];
        strncpy(name, pName->z, pName->n);
        name[pName->n] = 0;

        update_names(zDatabase, name, NULL);
    }

    void maxscaleUse(Parse* pParse, Token* pToken)
    {
        ss_dassert(this_thread.initialized);

        m_status = QC_QUERY_PARSED;
        m_type_mask = QUERY_TYPE_SESSION_WRITE;
        m_operation = QUERY_OP_CHANGE_DB;
    }

private:
    QcSqliteInfo(uint32_t cllct)
        : m_status(QC_QUERY_INVALID)
        , m_collect(cllct)
        , m_collected(0)
        , m_pQuery(NULL)
        , m_nQuery(0)
        , m_type_mask(QUERY_TYPE_UNKNOWN)
        , m_operation(QUERY_OP_UNDEFINED)
        , m_has_clause(false)
        , m_pzTable_names(NULL)
        , m_table_names_len(0)
        , m_table_names_capacity(0)
        , m_pzTable_fullnames(NULL)
        , m_table_fullnames_len(0)
        , m_table_fullnames_capacity(0)
        , m_zCreated_table_name(NULL)
        , m_is_drop_table(false)
        , m_pzDatabase_names(NULL)
        , m_database_names_len(0)
        , m_database_names_capacity(0)
        , m_keyword_1(0) // Sqlite3 starts numbering tokens from 1, so 0 means
        , m_keyword_2(0) // that we have not seen a keyword.
        , m_zPrepare_name(NULL)
        , m_pPreparable_stmt(NULL)
        , m_pField_infos(NULL)
        , m_field_infos_len(0)
        , m_field_infos_capacity(0)
        , m_pFunction_infos(NULL)
        , m_function_infos_len(0)
        , m_function_infos_capacity(0)
        , m_sql_mode(this_thread.sql_mode)
        , m_pFunction_name_mappings(this_thread.pFunction_name_mappings)
    {
    }

private:
    bool should_collect(qc_collect_info_t collect) const
    {
        return ((m_collect & collect) && !(m_collected & collect));
    }

    static void free_field_infos(QC_FIELD_INFO* pInfos, size_t nInfos)
    {
        if (pInfos)
        {
            for (size_t i = 0; i < nInfos; ++i)
            {
                MXS_FREE(pInfos[i].database);
                MXS_FREE(pInfos[i].table);
                MXS_FREE(pInfos[i].column);
            }

            MXS_FREE(pInfos);
        }
    }

    static void free_function_infos(QC_FUNCTION_INFO* pInfos, size_t nInfos)
    {
        if (pInfos)
        {
            for (size_t i = 0; i < nInfos; ++i)
            {
                MXS_FREE(pInfos[i].name);
            }

            MXS_FREE(pInfos);
        }
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

    static char** copy_string_array(char** pzStrings, int* pn)
    {
        size_t n = 0;

        char** pz = pzStrings;
        *pn = 0;

        while (*pz)
        {
            ++pz;
            ++(*pn);
        }

        pz = (char**) MXS_MALLOC((*pn + 1) * sizeof(char*));
        MXS_ABORT_IF_NULL(pz);

        pz[*pn] = 0;

        for (int i = 0; i < *pn; ++i)
        {
            pz[i] = MXS_STRDUP(pzStrings[i]);
            MXS_ABORT_IF_NULL(pz[i]);
        }

        return pz;
    }

    const char* table_name_collected(const char* zTable)
    {
        size_t i = 0;

        while ((i < m_table_names_len) && (strcmp(m_pzTable_names[i], zTable) != 0))
        {
            ++i;
        }

        return (i != m_table_names_len) ? m_pzTable_names[i] : NULL;
    }

    const char* table_fullname_collected(const char* zTable)
    {
        size_t i = 0;

        while ((i < m_table_fullnames_len) && (strcmp(m_pzTable_fullnames[i], zTable) != 0))
        {
            ++i;
        }

        return (i != m_table_fullnames_len) ? m_pzTable_fullnames[i] : NULL;
    }

    const char* database_name_collected(const char* zDatabase)
    {
        size_t i = 0;

        while ((i < m_database_names_len) && (strcmp(m_pzDatabase_names[i], zDatabase) != 0))
        {
            ++i;
        }

        return (i != m_database_names_len) ? m_pzDatabase_names[i] : NULL;
    }

    const char* update_table_names(const char* zDatabase, size_t nDatabase,
                                   const char* zTable, size_t nTable)
    {
        ss_dassert(zTable && nTable);

        const char* zCollected_table = table_name_collected(zTable);

        if (!zCollected_table)
        {
            char* zCopy = MXS_STRDUP_A(zTable);

            enlarge_string_array(1, m_table_names_len, &m_pzTable_names, &m_table_names_capacity);
            m_pzTable_names[m_table_names_len++] = zCopy;
            m_pzTable_names[m_table_names_len] = NULL;

            zCollected_table = zCopy;
        }

        char fullname[nDatabase + 1 + nTable + 1];

        if (nDatabase)
        {
            strcpy(fullname, zDatabase);
            strcat(fullname, ".");
        }
        else
        {
            fullname[0] = 0;
        }

        strcat(fullname, zTable);

        if (!table_fullname_collected(fullname))
        {
            char* zCopy = MXS_STRDUP_A(fullname);

            enlarge_string_array(1, m_table_fullnames_len,
                                 &m_pzTable_fullnames, &m_table_fullnames_capacity);
            m_pzTable_fullnames[m_table_fullnames_len++] = zCopy;
            m_pzTable_fullnames[m_table_fullnames_len] = NULL;
        }

        return zCollected_table;
    }

    const char* update_database_names(const char* zDatabase)
    {
        ss_dassert(zDatabase);
        ss_dassert(strlen(zDatabase) != 0);

        const char* zCollected_database = database_name_collected(zDatabase);

        if (!zCollected_database)
        {
            char* zCopy = MXS_STRDUP_A(zDatabase);

            enlarge_string_array(1, m_database_names_len,
                                 &m_pzDatabase_names, &m_database_names_capacity);
            m_pzDatabase_names[m_database_names_len++] = zCopy;
            m_pzDatabase_names[m_database_names_len] = NULL;

            zCollected_database = zCopy;
        }

        return zCollected_database;
    }

public:
    typedef std::map<std::string, QcAliasValue> Aliases;

    // TODO: Make these private once everything's been updated.
    qc_parse_result_t m_status;                 // The validity of the information in this structure.
    uint32_t m_collect;                         // What information should be collected.
    uint32_t m_collected;                       // What information has been collected.
    const char* m_pQuery;                       // The query passed to sqlite.
    size_t m_nQuery;                            // The length of the query.

    uint32_t m_type_mask;                       // The type mask of the query.
    qc_query_op_t m_operation;                  // The operation in question.
    bool m_has_clause;                          // Has WHERE or HAVING.
    char** m_pzTable_names;                     // Array of table names used in the query.
    size_t m_table_names_len;                   // The used entries in m_pzTable_names.
    size_t m_table_names_capacity;              // The capacity of m_pzTable_names.
    char** m_pzTable_fullnames;                 // Array of qualified table names used in the query.
    size_t m_table_fullnames_len;               // The used entries in m_pzTable_fullnames.
    size_t m_table_fullnames_capacity;          // The capacity of m_table_fullnames.
    char* m_zCreated_table_name;                // The name of a created table.
    bool m_is_drop_table;                       // Is the query a DROP TABLE.
    char** m_pzDatabase_names;                  // Array of database names used in the query.
    size_t m_database_names_len;                // The used entries in m_database_names.
    size_t m_database_names_capacity;           // The capacity of m_database_names.
    int m_keyword_1;                            // The first encountered keyword.
    int m_keyword_2;                            // The second encountered keyword.
    char* m_zPrepare_name;                      // The name of a prepared statement.
    GWBUF* m_pPreparable_stmt;                  // The preparable statement.
    QC_FIELD_INFO *m_pField_infos;              // Pointer to array of QC_FIELD_INFOs.
    size_t m_field_infos_len;                   // The used entries in field_infos.
    size_t m_field_infos_capacity;              // The capacity of the field_infos array.
    QC_FUNCTION_INFO *m_pFunction_infos;        // Pointer to array of QC_FUNCTION_INFOs.
    size_t m_function_infos_len;                // The used entries in function_infos.
    size_t m_function_infos_capacity;           // The capacity of the function_infos array.
    qc_sql_mode_t m_sql_mode;                   // The current sql_mode.
    QC_NAME_MAPPING* m_pFunction_name_mappings; // How function names should be mapped.
    Aliases m_aliases;                          // Alias names for tables
};

extern "C"
{

extern void mxs_sqlite3AlterFinishAddColumn(Parse *, Token *);
extern void mxs_sqlite3AlterBeginAddColumn(Parse *, SrcList *);
extern void mxs_sqlite3Analyze(Parse *, SrcList *);
extern void mxs_sqlite3BeginTransaction(Parse*, int token, int type);
extern void mxs_sqlite3CommitTransaction(Parse*);
extern void mxs_sqlite3CreateIndex(Parse*,Token*,Token*,SrcList*,ExprList*,int,Token*,
                                   Expr*, int, int);
extern void mxs_sqlite3BeginTrigger(Parse*, Token*,Token*,int,int,IdList*,SrcList*,
                                    Expr*,int, int);
extern void mxs_sqlite3FinishTrigger(Parse*, TriggerStep*, Token*);
extern void mxs_sqlite3CreateView(Parse*,Token*,Token*,Token*,ExprList*,Select*,int,int);
extern void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing);
extern void mxs_sqlite3DropIndex(Parse*, SrcList*, SrcList*,int);
extern void mxs_sqlite3DropTable(Parse*, SrcList*, int, int, int);
extern void mxs_sqlite3EndTable(Parse*, Token*, Token*, u8, Select*, SrcList*);
extern void mxs_sqlite3Insert(Parse*, SrcList*, Select*, IdList*, int,ExprList*);
extern void mxs_sqlite3RollbackTransaction(Parse*);
extern void mxs_sqlite3Savepoint(Parse *pParse, int op, Token *pName);
extern int  mxs_sqlite3Select(Parse*, Select*, SelectDest*);
extern void mxs_sqlite3StartTable(Parse*,Token*,Token*,int,int,int,int);
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
extern void maxscaleExplain(Parse*, Token* pNext);
extern void maxscaleFlush(Parse*, Token* pWhat);
extern void maxscaleHandler(Parse*, mxs_handler_t, SrcList* pFullName, Token* pName);
extern void maxscaleLoadData(Parse*, SrcList* pFullName);
extern void maxscaleLock(Parse*, mxs_lock_t, SrcList*);
extern void maxscalePrepare(Parse*, Token* pName, Expr* pStmt);
extern void maxscalePrivileges(Parse*, int kind);
extern void maxscaleRenameTable(Parse*, SrcList* pTables);
extern void maxscaleSet(Parse*, int scope, mxs_set_t kind, ExprList*);
extern void maxscaleShow(Parse*, MxsShow* pShow);
extern void maxscaleTruncate(Parse*, Token* pDatabase, Token* pName);
extern void maxscaleUse(Parse*, Token*);

extern void maxscale_update_function_info(const char* name, unsigned usage);

extern void maxscaleComment();
extern int maxscaleKeyword(int token);
extern int maxscaleTranslateKeyword(int token);

}

/**
 * Used for freeing a QcSqliteInfo object added to a GWBUF.
 *
 * @param object A pointer to a QcSqliteInfo object.
 */
static void buffer_object_free(void* pData)
{
    QcSqliteInfo* pInfo = static_cast<QcSqliteInfo*>(pData);
    delete pInfo;
}

static void enlarge_string_array(size_t n, size_t len, char*** ppzStrings, size_t* pCapacity)
{
    if (len + n >= *pCapacity)
    {
        int capacity = *pCapacity ? *pCapacity * 2 : 4;

        *ppzStrings = (char**) MXS_REALLOC(*ppzStrings, capacity * sizeof(char**));
        MXS_ABORT_IF_NULL(*ppzStrings);
        *pCapacity = capacity;
    }
}

static bool ensure_query_is_parsed(GWBUF* query, uint32_t collect)
{
    bool parsed = query_is_parsed(query, collect);

    if (!parsed)
    {
        parsed = parse_query(query, collect);
    }

    return parsed;
}

static void parse_query_string(const char* query, int len)
{
    sqlite3_stmt* stmt = NULL;
    const char* tail = NULL;

    ss_dassert(this_thread.pDb);
    int rc = sqlite3_prepare(this_thread.pDb, query, len, &stmt, &tail);

    const int max_len = 512; // Maximum length of logged statement.
    const int l = (len > max_len ? max_len : len);
    const char* suffix = (len > max_len ? "..." : "");
    const char* format;

    if (this_thread.pInfo->m_operation == QUERY_OP_EXPLAIN)
    {
        this_thread.pInfo->m_status = QC_QUERY_PARSED;
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

                // The status was set to QC_QUERY_PARSED, but sqlite3 returned an
                // error. Most likely, query contains some excess unrecognized stuff.
                this_thread.pInfo->m_status = QC_QUERY_PARTIALLY_PARSED;
            }
            else
            {
                format =
                    "Statement was neither parsed nor recognized from keywords "
                    "(Sqlite3 error: %s, %s): \"%.*s%s\"";
            }
        }

        if (this_unit.log_level > QC_LOG_NOTHING)
        {
            bool log_warning = false;

            switch (this_unit.log_level)
            {
            case QC_LOG_NON_PARSED:
                log_warning = this_thread.pInfo->m_status < QC_QUERY_PARSED;
                break;

            case QC_LOG_NON_PARTIALLY_PARSED:
                log_warning = this_thread.pInfo->m_status < QC_QUERY_PARTIALLY_PARSED;
                break;

            case QC_LOG_NON_TOKENIZED:
                log_warning = this_thread.pInfo->m_status < QC_QUERY_TOKENIZED;
                break;

            default:
                ss_dassert(!true);
                break;
            }

            if (log_warning)
            {
                MXS_WARNING(format, sqlite3_errstr(rc), sqlite3_errmsg(this_thread.pDb), l, query, suffix);
            }
        }
    }
    else if (this_thread.initialized) // If we are initializing, the query will not be classified.
    {
        if (this_unit.log_level > QC_LOG_NOTHING)
        {
            if (qc_info_was_tokenized(this_thread.pInfo->m_status))
            {
                // This suggests a callback from the parser into this module is not made.
                format =
                    "Statement was classified only based on keywords, "
                    "even though the statement was parsed: \"%.*s%s\"";

                MXS_WARNING(format, l, query, suffix);
            }
            else if (!qc_info_was_parsed(this_thread.pInfo->m_status))
            {
                // This suggests there are keywords that should be recognized but are not,
                // a tentative classification cannot be (or is not) made using the keywords
                // seen and/or a callback from the parser into this module is not made.
                format = "Statement was parsed, but not classified: \"%.*s%s\"";

                MXS_WARNING(format, l, query, suffix);
            }
        }
    }

    if (stmt)
    {
        sqlite3_finalize(stmt);
    }
}

static bool parse_query(GWBUF* query, uint32_t collect)
{
    bool parsed = false;
    ss_dassert(!query_is_parsed(query, collect));

    if (GWBUF_IS_CONTIGUOUS(query))
    {
        uint8_t* data = (uint8_t*) GWBUF_DATA(query);

        if ((GWBUF_LENGTH(query) >= MYSQL_HEADER_LEN + 1) &&
            (GWBUF_LENGTH(query) == MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(data)))
        {
            uint8_t command = MYSQL_GET_COMMAND(data);

            if ((command == MYSQL_COM_QUERY) || (command == MYSQL_COM_STMT_PREPARE))
            {
                QcSqliteInfo* pInfo =
                    (QcSqliteInfo*) gwbuf_get_buffer_object_data(query, GWBUF_PARSING_INFO);

                if (pInfo)
                {
                    ss_dassert((~pInfo->m_collect & collect) != 0);
                    ss_dassert((~pInfo->m_collected & collect) != 0);

                    // If we get here, then the statement has been parsed once, but
                    // not all needed was collected. Now we turn on all blinkenlichts to
                    // ensure that a statement is parsed at most twice.
                    pInfo->m_collect = QC_COLLECT_ALL;

                    // We also reset the collected keywords, so that code that behaves
                    // differently depending on whether keywords have been seem or not
                    // acts the same way on this second round.
                    pInfo->m_keyword_1 = 0;
                    pInfo->m_keyword_2 = 0;
                }
                else
                {
                    pInfo = QcSqliteInfo::create(collect);

                    if (pInfo)
                    {
                        // TODO: Add return value to gwbuf_add_buffer_object.
                        gwbuf_add_buffer_object(query, GWBUF_PARSING_INFO, pInfo, buffer_object_free);
                    }
                }

                if (pInfo)
                {
                    this_thread.pInfo = pInfo;

                    size_t len = MYSQL_GET_PAYLOAD_LEN(data) - 1; // Subtract 1 for packet type byte.

                    const char* s = (const char*) &data[MYSQL_HEADER_LEN + 1];

                    this_thread.pInfo->m_pQuery = s;
                    this_thread.pInfo->m_nQuery = len;
                    parse_query_string(s, len);
                    this_thread.pInfo->m_pQuery = NULL;
                    this_thread.pInfo->m_nQuery = 0;

                    if (command == MYSQL_COM_STMT_PREPARE)
                    {
                        pInfo->m_type_mask |= QUERY_TYPE_PREPARE_STMT;
                    }

                    pInfo->m_collected = pInfo->m_collect;

                    parsed = true;

                    this_thread.pInfo = NULL;
                }
                else
                {
                    MXS_ERROR("Could not allocate structure for containing parse data.");
                }
            }
            else
            {
                MXS_ERROR("The provided buffer does not contain a COM_QUERY, but a %s.",
                          STRPACKETTYPE(MYSQL_GET_COMMAND(data)));
            }
        }
        else
        {
            MXS_ERROR("Packet size %u, provided buffer is %ld.",
                      MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(data),
                      GWBUF_LENGTH(query));
        }
    }
    else
    {
        MXS_ERROR("Provided buffer is not contiguous.");
    }

    return parsed;
}

static bool query_is_parsed(GWBUF* query, uint32_t collect)
{
    bool rc = query && GWBUF_IS_PARSED(query);

    if (rc)
    {
        QcSqliteInfo* pInfo = (QcSqliteInfo*) gwbuf_get_buffer_object_data(query, GWBUF_PARSING_INFO);
        ss_dassert(pInfo);

        if ((~pInfo->m_collected & collect) != 0)
        {
            // The statement has been parsed once, but the needed information
            // was not collected at that time.
            rc = false;
        }
    }

    return rc;
}

/**
 * Logs information about invalid data.
 *
 * @param query   The query that could not be parsed.
 * @param message What is being asked for.
 */
static void log_invalid_data(GWBUF* query, const char* message)
{
    // At this point the query should be contiguous, but better safe than sorry.

    if (GWBUF_LENGTH(query) >= MYSQL_HEADER_LEN + 1)
    {
        char *sql;
        int length;

        if (modutil_extract_SQL(query, &sql, &length))
        {
            if (length > (int)GWBUF_LENGTH(query) - MYSQL_HEADER_LEN - 1)
            {
                length = (int)GWBUF_LENGTH(query) - MYSQL_HEADER_LEN - 1;
            }

            MXS_INFO("Parsing the query failed, %s: %*s", message, length, sql);
        }
    }
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

static void update_function_info(QcSqliteInfo* info,
                                 const char* name,
                                 uint32_t usage)
{
    ss_dassert(name);

    if (!(info->m_collect & QC_COLLECT_FUNCTIONS) || (info->m_collected & QC_COLLECT_FUNCTIONS))
    {
        // If function information should not be collected, or if function information
        // has already been collected, we just return.
        return;
    }

    name = map_function_name(info->m_pFunction_name_mappings, name);

    QC_FUNCTION_INFO item = { (char*)name, usage };

    size_t i;
    for (i = 0; i < info->m_function_infos_len; ++i)
    {
        QC_FUNCTION_INFO* function_info = info->m_pFunction_infos + i;

        if (strcasecmp(item.name, function_info->name) == 0)
        {
            break;
        }
    }

    QC_FUNCTION_INFO* function_infos = NULL;

    if (i == info->m_function_infos_len) // If true, the function was not present already.
    {
        if (info->m_function_infos_len < info->m_function_infos_capacity)
        {
            function_infos = info->m_pFunction_infos;
        }
        else
        {
            size_t capacity = info->m_function_infos_capacity ? 2 * info->m_function_infos_capacity : 8;
            function_infos = (QC_FUNCTION_INFO*)MXS_REALLOC(info->m_pFunction_infos,
                                                            capacity * sizeof(QC_FUNCTION_INFO));

            if (function_infos)
            {
                info->m_pFunction_infos = function_infos;
                info->m_function_infos_capacity = capacity;
            }
        }
    }
    else
    {
        info->m_pFunction_infos[i].usage |= usage;
    }

    // If function_infos is NULL, then the function was found and has already been noted.
    if (function_infos)
    {
        ss_dassert(item.name);
        item.name = MXS_STRDUP(item.name);

        if (item.name)
        {
            function_infos[info->m_function_infos_len++] = item;
        }
    }
}

extern void maxscale_update_function_info(const char* name, uint32_t usage)
{
    QcSqliteInfo* info = this_thread.pInfo;

    update_function_info(info, name, usage);
}

static void update_field_infos_from_expr(QcSqliteInfo* info,
                                         const Expr* pExpr,
                                         uint32_t usage,
                                         const ExprList* pExclude)
{
    QC_FIELD_INFO item = {};

    if (pExpr->op == TK_ASTERISK)
    {
        item.column = (char*)"*";
    }
    else if (pExpr->op == TK_ID)
    {
        // select a from...
        item.column = pExpr->u.zToken;
    }
    else if (pExpr->op == TK_DOT)
    {
        if (pExpr->pLeft->op == TK_ID &&
            (pExpr->pRight->op == TK_ID || pExpr->pRight->op == TK_ASTERISK))
        {
            // select a.b from...
            item.table = pExpr->pLeft->u.zToken;
            if (pExpr->pRight->op == TK_ID)
            {
                item.column = pExpr->pRight->u.zToken;
            }
            else
            {
                item.column = (char*)"*";
            }
        }
        else if (pExpr->pLeft->op == TK_ID &&
                 pExpr->pRight->op == TK_DOT &&
                 pExpr->pRight->pLeft->op == TK_ID &&
                 (pExpr->pRight->pRight->op == TK_ID || pExpr->pRight->pRight->op == TK_ASTERISK))
        {
            // select a.b.c from...
            item.database = pExpr->pLeft->u.zToken;
            item.table = pExpr->pRight->pLeft->u.zToken;
            if (pExpr->pRight->pRight->op == TK_ID)
            {
                item.column = pExpr->pRight->pRight->u.zToken;
            }
            else
            {
                item.column = (char*)"*";
            }
        }
    }

    if (item.column)
    {
        bool should_update = true;

        if ((pExpr->flags & EP_DblQuoted) == 0)
        {
            if ((strcasecmp(item.column, "true") == 0) || (strcasecmp(item.column, "false") == 0))
            {
                should_update = false;
            }
        }

        if (should_update)
        {
            info->update_field_info(item.database, item.table, item.column, usage, pExclude);
        }
    }
}

static void update_field_infos_from_with(QcSqliteInfo* info,
                                         const With* pWith)
{
    for (int i = 0; i < pWith->nCte; ++i)
    {
        const With::Cte* pCte = &pWith->a[i];

        if (pCte->pSelect)
        {
            update_field_infos_from_select(info, pCte->pSelect, QC_USED_IN_SUBSELECT, NULL);
        }
    }
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

    case TK_IN:
        return "in";

    case TK_ISNULL:
        return "isnull";

    case TK_MINUS:
        return "-";

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
        ss_dassert(!true);
        return "";
    }
}

static void update_field_infos(QcSqliteInfo* info,
                               int prev_token,
                               const Expr* pExpr,
                               uint32_t usage,
                               qc_token_position_t pos,
                               const ExprList* pExclude)
{
    const Expr* pLeft = pExpr->pLeft;
    const Expr* pRight = pExpr->pRight;
    const char* zToken = pExpr->u.zToken;

    bool ignore_exprlist = false;

    switch (pExpr->op)
    {
    case TK_ASTERISK: // select *
        update_field_infos_from_expr(info, pExpr, usage, pExclude);
        break;

    case TK_DOT: // select a.b ... select a.b.c
        update_field_infos_from_expr(info, pExpr, usage, pExclude);
        break;

    case TK_ID: // select a
        update_field_infos_from_expr(info, pExpr, usage, pExclude);
        break;

    case TK_VARIABLE:
        {
            if (zToken[0] == '@')
            {
                if (zToken[1] == '@')
                {
                    if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT))
                    {
                        info->m_type_mask |= QUERY_TYPE_GSYSVAR_WRITE;
                    }
                    else
                    {
                        if ((strcasecmp(&zToken[2], "identity") == 0) ||
                            (strcasecmp(&zToken[2], "last_insert_id") == 0))
                        {
                            info->m_type_mask |= QUERY_TYPE_MASTER_READ;
                        }
                        else
                        {
                            info->m_type_mask |= QUERY_TYPE_SYSVAR_READ;
                        }
                    }
                }
                else
                {
                    if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT))
                    {
                        info->m_type_mask |= QUERY_TYPE_USERVAR_WRITE;
                    }
                    else
                    {
                        info->m_type_mask |= QUERY_TYPE_USERVAR_READ;
                    }
                }
            }
            else if (zToken[0] != '?')
            {
                MXS_WARNING("%s reported as VARIABLE.", zToken);
            }
        }
        break;

    default:
        MXS_DEBUG("Token %d not handled explicitly.", pExpr->op);
    // Fallthrough intended.
    case TK_BETWEEN:
    case TK_CASE:
    case TK_EXISTS:
    case TK_FUNCTION:
    case TK_IN:
    case TK_SELECT:
        switch (pExpr->op)
        {
        case TK_EQ:
            // We don't report "=" if it's not used in a specific context (SELECT, WHERE)
            // and if it is used in SET. We also exclude it it in a context where a
            // variable is set.
            if (((usage != 0) && (usage != QC_USED_IN_SET)) &&
                (!pExpr->pLeft || (pExpr->pLeft->op != TK_VARIABLE)))
            {
                update_function_info(info, get_token_symbol(pExpr->op), usage);
            }
            break;

        case TK_GE:
        case TK_GT:
        case TK_LE:
        case TK_LT:
        case TK_NE:

        case TK_BETWEEN:
        case TK_BITAND:
        case TK_BITOR:
        case TK_CASE:
        case TK_IN:
        case TK_ISNULL:
        case TK_MINUS:
        case TK_NOTNULL:
        case TK_PLUS:
        case TK_SLASH:
        case TK_STAR:
            update_function_info(info, get_token_symbol(pExpr->op), usage);
            break;

        case TK_REM:
            if (info->m_sql_mode == QC_SQL_MODE_ORACLE)
            {
                if ((pLeft && (pLeft->op == TK_ID)) &&
                    (pRight && (pRight->op == TK_ID)) &&
                    (strcasecmp(pLeft->u.zToken, "sql") == 0) &&
                    (strcasecmp(pRight->u.zToken, "rowcount") == 0))
                {
                    char sqlrowcount[13]; // strlen("sql") + strlen("%") + strlen("rowcount") + 1
                    sprintf(sqlrowcount, "%s%%%s", pLeft->u.zToken, pRight->u.zToken);

                    update_function_info(info, sqlrowcount, usage);

                    pLeft = NULL;
                    pRight = NULL;
                }
                else
                {
                    update_function_info(info, get_token_symbol(pExpr->op), usage);
                }
            }
            else
            {
                update_function_info(info, get_token_symbol(pExpr->op), usage);
            }
            break;

        case TK_UMINUS:
            switch (this_unit.parse_as)
            {
            case QC_PARSE_AS_DEFAULT:
                update_function_info(info, get_token_symbol(pExpr->op), usage);
                break;

            case QC_PARSE_AS_103:
                // In MariaDB 10.3 a unary minus is not considered a function.
                break;

            default:
                ss_dassert(!true);
            }
            break;

        case TK_FUNCTION:
            if (zToken)
            {
                if (strcasecmp(zToken, "last_insert_id") == 0)
                {
                    info->m_type_mask |= (QUERY_TYPE_READ | QUERY_TYPE_MASTER_READ);
                }
                else if (info->is_sequence_related_function(zToken))
                {
                    info->m_type_mask |= QUERY_TYPE_WRITE;
                    ignore_exprlist = true;
                }
                else if (!is_builtin_readonly_function(zToken,
                                                       this_thread.version_major,
                                                       this_thread.version_minor,
                                                       this_thread.version_patch,
                                                       info->m_sql_mode == QC_SQL_MODE_ORACLE))
                {
                    info->m_type_mask |= QUERY_TYPE_WRITE;
                }

                // We exclude "row", because we cannot detect all rows the same
                // way qc_mysqlembedded does.
                if (!ignore_exprlist && (strcasecmp(zToken, "row") != 0))
                {
                    update_function_info(info, zToken, usage);
                }
            }
            break;

        default:
            break;
        }

        if (pLeft)
        {
            update_field_infos(info, pExpr->op, pExpr->pLeft, usage, QC_TOKEN_LEFT, pExclude);
        }

        if (pRight)
        {
            if (usage & QC_USED_IN_SET)
            {
                usage &= ~QC_USED_IN_SET;
            }

            update_field_infos(info, pExpr->op, pExpr->pRight, usage, QC_TOKEN_RIGHT, pExclude);
        }

        if (pExpr->x.pList)
        {
            switch (pExpr->op)
            {
            case TK_BETWEEN:
            case TK_CASE:
            case TK_FUNCTION:
                if (!ignore_exprlist)
                {
                    update_field_infos_from_exprlist(info, pExpr->x.pList, usage, pExclude);
                }
                break;

            case TK_EXISTS:
            case TK_IN:
            case TK_SELECT:
                if (pExpr->flags & EP_xIsSelect)
                {
                    uint32_t sub_usage = usage;

                    sub_usage &= ~QC_USED_IN_SELECT;
                    sub_usage |= QC_USED_IN_SUBSELECT;
                    update_field_infos_from_select(info, pExpr->x.pSelect, sub_usage, pExclude);
                }
                else
                {
                    update_field_infos_from_exprlist(info, pExpr->x.pList, usage, pExclude);
                }
                break;
            }
        }
        break;
    }
}

static void update_field_infos_from_exprlist(QcSqliteInfo* info,
                                             const ExprList* pEList,
                                             uint32_t usage,
                                             const ExprList* pExclude)
{
    for (int i = 0; i < pEList->nExpr; ++i)
    {
        ExprList::ExprList_item* pItem = &pEList->a[i];

        update_field_infos(info, 0, pItem->pExpr, usage, QC_TOKEN_MIDDLE, pExclude);
    }
}

static void update_field_infos_from_idlist(QcSqliteInfo* info,
                                           const IdList* pIds,
                                           uint32_t usage,
                                           const ExprList* pExclude)
{
    for (int i = 0; i < pIds->nId; ++i)
    {
        IdList::IdList_item* pItem = &pIds->a[i];

        info->update_field_info(NULL, NULL, pItem->zName, usage, pExclude);
    }
}

static void update_field_infos_from_select(QcSqliteInfo* pInfo,
                                           const Select* pSelect,
                                           uint32_t usage,
                                           const ExprList* pExclude)
{
    if (pSelect->pSrc)
    {
        const SrcList* pSrc = pSelect->pSrc;

        for (int i = 0; i < pSrc->nSrc; ++i)
        {
            if (pSrc->a[i].zName)
            {
                pInfo->update_names(pSrc->a[i].zDatabase, pSrc->a[i].zName, pSrc->a[i].zAlias);
            }

            if (pSrc->a[i].pSelect)
            {
                uint32_t sub_usage = usage;

                sub_usage &= ~QC_USED_IN_SELECT;
                sub_usage |= QC_USED_IN_SUBSELECT;

                update_field_infos_from_select(pInfo, pSrc->a[i].pSelect, sub_usage, pExclude);
            }

#ifdef QC_COLLECT_NAMES_FROM_USING
            // With this enabled, the affected fields of
            //    select * from (t1 as t2 left join t1 as t3 using (a)), t1;
            // will be "* a", otherwise "*". However, that "a" is used in the join
            // does not reveal its value, right?
            if (pSrc->a[i].pUsing)
            {
                update_field_infos_from_idlist(pInfo, pSrc->a[i].pUsing, 0, pSelect->pEList);
            }
#endif
        }
    }

    if (pSelect->pEList)
    {
        update_field_infos_from_exprlist(pInfo, pSelect->pEList, usage, NULL);
    }

    if (pSelect->pWhere)
    {
        pInfo->m_has_clause = true;
        update_field_infos(pInfo, 0, pSelect->pWhere, QC_USED_IN_WHERE, QC_TOKEN_MIDDLE, pSelect->pEList);
    }

    if (pSelect->pGroupBy)
    {
        update_field_infos_from_exprlist(pInfo, pSelect->pGroupBy, QC_USED_IN_GROUP_BY, pSelect->pEList);
    }

    if (pSelect->pHaving)
    {
        pInfo->m_has_clause = true;
#if defined(COLLECT_HAVING_AS_WELL)
        // A HAVING clause can only refer to fields that already have been
        // mentioned. Consequently, they need not be collected.
        update_field_infos(pInfo, 0, pSelect->pHaving, 0, QC_TOKEN_MIDDLE, pSelect->pEList);
#endif
    }

    if (pSelect->pWith)
    {
        update_field_infos_from_with(pInfo, pSelect->pWith);
    }

    if ((pSelect->op == TK_UNION) && pSelect->pPrior)
    {
        update_field_infos_from_select(pInfo, pSelect->pPrior, usage, pExclude);
    }
}

static void update_names_from_srclist(QcSqliteInfo* pInfo, const SrcList* pSrc)
{
    for (int i = 0; i < pSrc->nSrc; ++i)
    {
        if (pSrc->a[i].zName)
        {
            pInfo->update_names(pSrc->a[i].zDatabase, pSrc->a[i].zName, pSrc->a[i].zAlias);
        }

        if (pSrc->a[i].pSelect && pSrc->a[i].pSelect->pSrc)
        {
            update_names_from_srclist(pInfo, pSrc->a[i].pSelect->pSrc);
        }
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
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3AlterFinishAddColumn(pParse, pToken));
}

void mxs_sqlite3AlterBeginAddColumn(Parse* pParse, SrcList* pSrcList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3AlterBeginAddColumn(pParse, pSrcList));
}

void mxs_sqlite3Analyze(Parse* pParse, SrcList* pSrcList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Analyze(pParse, pSrcList));
}

void mxs_sqlite3BeginTransaction(Parse* pParse, int token, int type)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3BeginTransaction(pParse, token, type));
}

void mxs_sqlite3BeginTrigger(Parse *pParse,      /* The parse context of the CREATE TRIGGER statement */
                             Token *pName1,      /* The name of the trigger */
                             Token *pName2,      /* The name of the trigger */
                             int tr_tm,          /* One of TK_BEFORE, TK_AFTER, TK_INSTEAD */
                             int op,             /* One of TK_INSERT, TK_UPDATE, TK_DELETE */
                             IdList *pColumns,   /* column list if this is an UPDATE OF trigger */
                             SrcList *pTableName,/* The name of the table/view the trigger applies to */
                             Expr *pWhen,        /* WHEN clause */
                             int isTemp,         /* True if the TEMPORARY keyword is present */
                             int noErr)          /* Suppress errors if the trigger already exists */
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3BeginTrigger(pParse, pName1, pName2, tr_tm, op,
                                                      pColumns, pTableName, pWhen, isTemp, noErr));
}

void mxs_sqlite3CommitTransaction(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3CommitTransaction(pParse));
}

void mxs_sqlite3CreateIndex(Parse *pParse,     /* All information about this parse */
                            Token *pName1,     /* First part of index name. May be NULL */
                            Token *pName2,     /* Second part of index name. May be NULL */
                            SrcList *pTblName, /* Table to index. Use pParse->pNewTable if 0 */
                            ExprList *pList,   /* A list of columns to be indexed */
                            int onError,       /* OE_Abort, OE_Ignore, OE_Replace, or OE_None */
                            Token *pStart,     /* The CREATE token that begins this statement */
                            Expr *pPIWhere,    /* WHERE clause for partial indices */
                            int sortOrder,     /* Sort order of primary key when pList==NULL */
                            int ifNotExist)    /* Omit error if index already exists */
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3CreateIndex(pParse, pName1, pName2, pTblName, pList,
                                                     onError, pStart, pPIWhere, sortOrder, ifNotExist));
}

void mxs_sqlite3CreateView(Parse *pParse,     /* The parsing context */
                           Token *pBegin,     /* The CREATE token that begins the statement */
                           Token *pName1,     /* The token that holds the name of the view */
                           Token *pName2,     /* The token that holds the name of the view */
                           ExprList *pCNames, /* Optional list of view column names */
                           Select *pSelect,   /* A SELECT statement that will become the new view */
                           int isTemp,        /* TRUE for a TEMPORARY view */
                           int noErr)         /* Suppress error messages if VIEW already exists */
{
    QC_TRACE();
    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3CreateView(pParse, pBegin, pName1, pName2,
                                                    pCNames, pSelect, isTemp, noErr));
}

void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3DeleteFrom(pParse, pTabList, pWhere, pUsing));
}

void mxs_sqlite3DropIndex(Parse* pParse, SrcList* pName, SrcList* pTable, int bits)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3DropIndex(pParse, pName, pTable, bits));
}

void mxs_sqlite3DropTable(Parse *pParse, SrcList *pName, int isView, int noErr, int isTemp)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3DropTable(pParse, pName, isView, noErr, isTemp));
}

void mxs_sqlite3EndTable(Parse *pParse,    /* Parse context */
                         Token *pCons,     /* The ',' token after the last column defn. */
                         Token *pEnd,      /* The ')' before options in the CREATE TABLE */
                         u8 tabOpts,       /* Extra table options. Usually 0. */
                         Select *pSelect,  /* Select from a "CREATE ... AS SELECT" */
                         SrcList* pOldTable) /* The old table in "CREATE ... LIKE OldTable" */
{
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        ss_dassert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3EndTable(pParse, pCons, pEnd, tabOpts, pSelect, pOldTable));
    }
    else
    {
        exposed_sqlite3EndTable(pParse, pCons, pEnd, tabOpts, pSelect);
    }
}

void mxs_sqlite3FinishTrigger(Parse *pParse,          /* Parser context */
                              TriggerStep *pStepList, /* The triggered program */
                              Token *pAll)            /* Token that describes the complete CREATE TRIGGER */
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

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Insert(pParse, pTabList, pSelect, pColumns, onError, pSet));
}

void mxs_sqlite3RollbackTransaction(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3RollbackTransaction(pParse));
}

int mxs_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest)
{
    int rc = -1;
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        ss_dassert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Select(pParse, p, pDest));
    }
    else
    {
        rc = exposed_sqlite3Select(pParse, p, pDest);
    }

    return rc;
}

void mxs_sqlite3StartTable(Parse *pParse,   /* Parser context */
                           Token *pName1,   /* First part of the name of the table or view */
                           Token *pName2,   /* Second part of the name of the table or view */
                           int isTemp,      /* True if this is a TEMP table */
                           int isView,      /* True if this is a VIEW */
                           int isVirtual,   /* True if this is a VIRTUAL table */
                           int noErr)       /* Do nothing if table already exists */
{
    QC_TRACE();

    if (this_thread.initialized)
    {
        QcSqliteInfo* pInfo = this_thread.pInfo;
        ss_dassert(pInfo);

        QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3StartTable(pParse, pName1, pName2,
                                                        isTemp, isView, isVirtual, noErr));
    }
    else
    {
        exposed_sqlite3StartTable(pParse, pName1, pName2, isTemp, isView, isVirtual, noErr);
    }
}

void mxs_sqlite3Update(Parse* pParse, SrcList* pTabList, ExprList* pChanges, Expr* pWhere, int onError)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Update(pParse, pTabList, pChanges, pWhere, onError));
}

void mxs_sqlite3Savepoint(Parse *pParse, int op, Token *pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->mxs_sqlite3Savepoint(pParse, op, pName));
}

void maxscaleCollectInfoFromSelect(Parse* pParse, Select* pSelect, int sub_select)
{
    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCollectInfoFromSelect(pParse, pSelect, sub_select));
}

void maxscaleAlterTable(Parse *pParse,            /* Parser context. */
                        mxs_alter_t command,
                        SrcList *pSrc,            /* The table to rename. */
                        Token *pName)             /* The new table name (RENAME). */
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleAlterTable(pParse, command, pSrc, pName));
}

void maxscaleCall(Parse* pParse, SrcList* pName, ExprList* pExprList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCall(pParse, pName, pExprList));
}

void maxscaleCheckTable(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCheckTable(pParse, pTables));
}

void maxscaleCreateSequence(Parse* pParse, Token* pDatabase, Token* pTable)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleCreateSequence(pParse, pDatabase, pTable));
}

void maxscaleComment()
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleComment());
}

void maxscaleDeclare(Parse* pParse)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDeclare(pParse));
}

void maxscaleDeallocate(Parse* pParse, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDeallocate(pParse, pName));
}

void maxscaleDo(Parse* pParse, ExprList* pEList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDo(pParse, pEList));
}

void maxscaleDrop(Parse* pParse, int what, Token* pDatabase, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleDrop(pParse, what, pDatabase, pName));
}

void maxscaleExecute(Parse* pParse, Token* pName, int type_mask)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleExecute(pParse, pName, type_mask));
}

void maxscaleExecuteImmediate(Parse* pParse, Token* pName, ExprSpan* pExprSpan, int type_mask)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleExecuteImmediate(pParse, pName, pExprSpan, type_mask));
}

void maxscaleExplain(Parse* pParse, Token* pNext)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleExplain(pParse, pNext));
}

void maxscaleFlush(Parse* pParse, Token* pWhat)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleFlush(pParse, pWhat));
}

void maxscaleHandler(Parse* pParse, mxs_handler_t type, SrcList* pFullName, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleHandler(pParse, type, pFullName, pName));
}

void maxscaleLoadData(Parse* pParse, SrcList* pFullName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleLoadData(pParse, pFullName));
}

void maxscaleLock(Parse* pParse, mxs_lock_t type, SrcList* pTables)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleLock(pParse, type, pTables));
}

int maxscaleTranslateKeyword(int token)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

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
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(token = pInfo->maxscaleKeyword(token));

    return token;
}

void maxscaleRenameTable(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleRenameTable(pParse, pTables));
}

void maxscalePrepare(Parse* pParse, Token* pName, Expr* pStmt)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscalePrepare(pParse, pName, pStmt));
}

void maxscalePrivileges(Parse* pParse, int kind)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscalePrivileges(pParse, kind));
}

void maxscaleSet(Parse* pParse, int scope, mxs_set_t kind, ExprList* pList)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleSet(pParse, scope, kind, pList));
}

void maxscaleShow(Parse* pParse, MxsShow* pShow)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleShow(pParse, pShow));
}

void maxscaleTruncate(Parse* pParse, Token* pDatabase, Token* pName)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleTruncate(pParse, pDatabase, pName));
}

void maxscaleUse(Parse* pParse, Token* pToken)
{
    QC_TRACE();

    QcSqliteInfo* pInfo = this_thread.pInfo;
    ss_dassert(pInfo);

    QC_EXCEPTION_GUARD(pInfo->maxscaleUse(pParse, pToken));
}

/**
 * API
 */
static int32_t qc_sqlite_setup(qc_sql_mode_t sql_mode, const char* args);
static int32_t qc_sqlite_process_init(void);
static void qc_sqlite_process_end(void);
static int32_t qc_sqlite_thread_init(void);
static void qc_sqlite_thread_end(void);
static int32_t qc_sqlite_parse(GWBUF* query, uint32_t collect, int32_t* result);
static int32_t qc_sqlite_get_type_mask(GWBUF* query, uint32_t* typemask);
static int32_t qc_sqlite_get_operation(GWBUF* query, int32_t* op);
static int32_t qc_sqlite_get_created_table_name(GWBUF* query, char** name);
static int32_t qc_sqlite_is_drop_table_query(GWBUF* query, int32_t* is_drop_table);
static int32_t qc_sqlite_get_table_names(GWBUF* query, int32_t fullnames, char*** names, int* tblsize);
static int32_t qc_sqlite_get_canonical(GWBUF* query, char** canonical);
static int32_t qc_sqlite_query_has_clause(GWBUF* query, int32_t* has_clause);
static int32_t qc_sqlite_get_database_names(GWBUF* query, char*** names, int* sizep);
static int32_t qc_sqlite_get_preparable_stmt(GWBUF* stmt, GWBUF** preparable_stmt);
static void qc_sqlite_set_server_version(uint64_t version);
static void qc_sqlite_get_server_version(uint64_t* version);
static int32_t qc_sqlite_get_sql_mode(qc_sql_mode_t* sql_mode);
static int32_t qc_sqlite_set_sql_mode(qc_sql_mode_t sql_mode);

static bool get_key_and_value(char* arg, const char** pkey, const char** pvalue)
{
    char* p = strchr(arg, '=');

    if (p)
    {
        *p = 0;

        *pkey = trim(arg);
        *pvalue = trim(p + 1);
    }

    return p != NULL;
}

static const char ARG_LOG_UNRECOGNIZED_STATEMENTS[] = "log_unrecognized_statements";
static const char ARG_PARSE_AS[] = "parse_as";

static int32_t qc_sqlite_setup(qc_sql_mode_t sql_mode, const char* cargs)
{
    QC_TRACE();
    assert(!this_unit.setup);

    qc_log_level_t log_level = QC_LOG_NOTHING;
    qc_parse_as_t parse_as = (sql_mode == QC_SQL_MODE_ORACLE) ? QC_PARSE_AS_103 : QC_PARSE_AS_DEFAULT;
    QC_NAME_MAPPING* function_name_mappings = function_name_mappings_default;

    if (cargs)
    {
        char args[strlen(cargs) + 1];
        strcpy(args, cargs);

        char *p1;
        char *token = strtok_r(args, ",", &p1);

        while (token)
        {
            const char* key;
            const char* value;

            if (get_key_and_value(token, &key, &value))
            {
                if (strcmp(key, ARG_LOG_UNRECOGNIZED_STATEMENTS) == 0)
                {
                    char *end;

                    long l = strtol(value, &end, 0);

                    if ((*end == 0) && (l >= QC_LOG_NOTHING) && (l <= QC_LOG_NON_TOKENIZED))
                    {
                        log_level = static_cast<qc_log_level_t>(l);
                    }
                    else
                    {
                        MXS_WARNING("'%s' is not a number between %d and %d.",
                                    value, QC_LOG_NOTHING, QC_LOG_NON_TOKENIZED);
                    }
                }
                else if (strcmp(key, ARG_PARSE_AS) == 0)
                {
                    if (strcmp(value, "10.3") == 0)
                    {
                        parse_as = QC_PARSE_AS_103;
                        MXS_NOTICE("Parsing as 10.3.");
                    }
                    else
                    {
                        MXS_WARNING("'%s' is not a recognized value for '%s'. "
                                    "Parsing as pre-10.3.", value, key);
                    }
                }
                else
                {
                    MXS_WARNING("'%s' is not a recognized argument.", key);
                }
            }
            else
            {
                MXS_WARNING("'%s' is not a recognized argument string.", args);
            }

            token = strtok_r(NULL, ",", &p1);
        }
    }

    if (sql_mode == QC_SQL_MODE_ORACLE)
    {
        function_name_mappings = function_name_mappings_oracle;
    }
    else if (parse_as == QC_PARSE_AS_103)
    {
        function_name_mappings = function_name_mappings_103;
    }

    this_unit.setup = true;
    this_unit.log_level = log_level;
    this_unit.sql_mode = sql_mode;
    this_unit.parse_as = parse_as;
    this_unit.pFunction_name_mappings = function_name_mappings;

    return this_unit.setup ? QC_RESULT_OK : QC_RESULT_ERROR;
}

static int32_t qc_sqlite_process_init(void)
{
    QC_TRACE();
    assert(this_unit.setup);
    assert(!this_unit.initialized);

    if (sqlite3_initialize() == 0)
    {
        init_builtin_functions();

        this_unit.initialized = true;

        if (qc_sqlite_thread_init() == 0)
        {
            if (this_unit.log_level != QC_LOG_NOTHING)
            {
                const char* message;

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
                    ss_dassert(!true);
                }

                MXS_NOTICE("%s", message);
            }
        }
        else
        {
            this_unit.initialized = false;

            sqlite3_shutdown();
        }
    }
    else
    {
        MXS_ERROR("Failed to initialize sqlite3.");
    }

    return this_unit.initialized ? QC_RESULT_OK : QC_RESULT_ERROR;
}

static void qc_sqlite_process_end(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);

    finish_builtin_functions();

    qc_sqlite_thread_end();

    sqlite3_shutdown();
    this_unit.initialized = false;
}

static int32_t qc_sqlite_thread_init(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(!this_thread.initialized);

    // TODO: It may be sufficient to have a single in-memory database for all threads.
    int rc = sqlite3_open(":memory:", &this_thread.pDb);
    if (rc == SQLITE_OK)
    {
        this_thread.sql_mode = this_unit.sql_mode;
        this_thread.pFunction_name_mappings = this_unit.pFunction_name_mappings;

        MXS_INFO("In-memory sqlite database successfully opened for thread %lu.",
                 (unsigned long) pthread_self());

        QcSqliteInfo* pInfo = QcSqliteInfo::create(QC_COLLECT_ALL);

        if (pInfo)
        {
            this_thread.pInfo = pInfo;

            // With this statement we cause sqlite3 to initialize itself, so that it
            // is not done as part of the actual classification of data.
            const char* s = "CREATE TABLE __maxscale__internal__ (field int UNIQUE)";
            size_t len = strlen(s);

            this_thread.pInfo->m_pQuery = s;
            this_thread.pInfo->m_nQuery = len;
            parse_query_string(s, len);
            this_thread.pInfo->m_pQuery = NULL;
            this_thread.pInfo->m_nQuery = 0;

            delete this_thread.pInfo;
            this_thread.pInfo = NULL;

            this_thread.initialized = true;
            this_thread.version_major = 0;
            this_thread.version_minor = 0;
            this_thread.version_patch = 0;
        }
        else
        {
            sqlite3_close(this_thread.pDb);
            this_thread.pDb = NULL;
        }
    }
    else
    {
        MXS_ERROR("Failed to open in-memory sqlite database for thread %lu: %d, %s",
                  (unsigned long) pthread_self(), rc, sqlite3_errstr(rc));
    }

    return this_thread.initialized ? QC_RESULT_OK : QC_RESULT_ERROR;
}

static void qc_sqlite_thread_end(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    ss_dassert(this_thread.pDb);
    int rc = sqlite3_close(this_thread.pDb);

    if (rc != SQLITE_OK)
    {
        MXS_WARNING("The closing of the thread specific sqlite database failed: %d, %s",
                    rc, sqlite3_errstr(rc));
    }

    this_thread.pDb = NULL;
    this_thread.initialized = false;
}

static int32_t qc_sqlite_parse(GWBUF* pStmt, uint32_t collect, int32_t* pResult)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, collect);

    if (pInfo)
    {
        *pResult = pInfo->m_status;
    }
    else
    {
        *pResult = QC_QUERY_INVALID;
    }

    return pInfo ? QC_RESULT_OK : QC_RESULT_ERROR;
}

static int32_t qc_sqlite_get_type_mask(GWBUF* pStmt, uint32_t* pType_mask)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pType_mask = QUERY_TYPE_UNKNOWN;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_ESSENTIALS);

    if (pInfo)
    {
        if (pInfo->get_type_mask(pType_mask))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report query type");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_operation(GWBUF* pStmt, int32_t* pOp)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pOp = QUERY_OP_UNDEFINED;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_ESSENTIALS);

    if (pInfo)
    {
        if (pInfo->get_operation(pOp))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report query operation");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_created_table_name(GWBUF* pStmt, char** pzCreated_table_name)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pzCreated_table_name = NULL;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_TABLES);

    if (pInfo)
    {
        if (pInfo->get_created_table_name(pzCreated_table_name))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report created tables");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_is_drop_table_query(GWBUF* pStmt, int32_t* pIs_drop_table)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pIs_drop_table = 0;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_ESSENTIALS);

    if (pInfo)
    {
        if (pInfo->is_drop_table_query(pIs_drop_table))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report whether query is drop table");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_table_names(GWBUF* pStmt,
                                         int32_t fullnames,
                                         char*** ppzTable_names,
                                         int32_t* pnTable_names)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *ppzTable_names = NULL;
    *pnTable_names = 0;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_TABLES);

    if (pInfo)
    {
        if (pInfo->get_table_names(fullnames, ppzTable_names, pnTable_names))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report what tables are accessed");
        }
    }
    else
    {
        MXS_ERROR("The pStmt could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_canonical(GWBUF* pStmt, char** pzCanonical)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pzCanonical = NULL;

    MXS_ERROR("qc_get_canonical not implemented yet.");

    return rv;
}

static int32_t qc_sqlite_query_has_clause(GWBUF* pStmt, int32_t* pHas_clause)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pHas_clause = 0;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_ESSENTIALS);

    if (pInfo)
    {
        if (pInfo->query_has_clause(pHas_clause))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report whether the query has a where clause");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_database_names(GWBUF* pStmt, char*** ppzDatabase_names, int* pnDatabase_names)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *ppzDatabase_names = NULL;
    *pnDatabase_names = 0;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_DATABASES);

    if (pInfo)
    {
        if (pInfo->get_database_names(ppzDatabase_names, pnDatabase_names))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report what databases are accessed");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_prepare_name(GWBUF* pStmt, char** pzPrepare_name)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pzPrepare_name = NULL;
    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_ESSENTIALS);

    if (pInfo)
    {
        if (pInfo->get_prepare_name(pzPrepare_name))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report the name of a prepared statement");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

int32_t qc_sqlite_get_field_info(GWBUF* pStmt, const QC_FIELD_INFO** ppInfos, uint32_t* pnInfos)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *ppInfos = NULL;
    *pnInfos = 0;

    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_FIELDS);

    if (pInfo)
    {
        if (pInfo->get_field_info(ppInfos, pnInfos))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report field info");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

int32_t qc_sqlite_get_function_info(GWBUF* pStmt, const QC_FUNCTION_INFO** ppInfos, uint32_t* pnInfos)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *ppInfos = NULL;
    *pnInfos = 0;

    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_FUNCTIONS);

    if (pInfo)
    {
        if (pInfo->get_function_info(ppInfos, pnInfos))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report function info");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

int32_t qc_sqlite_get_preparable_stmt(GWBUF* pStmt, GWBUF** pzPreparable_stmt)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *pzPreparable_stmt = NULL;

    QcSqliteInfo* pInfo = QcSqliteInfo::get(pStmt, QC_COLLECT_ESSENTIALS);

    if (pInfo)
    {
        if (pInfo->get_preparable_stmt(pzPreparable_stmt))
        {
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(pStmt, "cannot report preperable statement");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static void qc_sqlite_set_server_version(uint64_t version)
{
    QC_TRACE();

    uint32_t major = version / 10000;
    uint32_t minor = (version - major * 10000) / 100;
    uint32_t patch = version - major * 10000 - minor * 100;

    this_thread.version_major = major;
    this_thread.version_minor = minor;
    this_thread.version_patch = patch;
}

static void qc_sqlite_get_server_version(uint64_t* pVersion)
{
    QC_TRACE();

    *pVersion =
        this_thread.version_major * 10000 +
        this_thread.version_minor * 100 +
        this_thread.version_patch;
}


int32_t qc_sqlite_get_sql_mode(qc_sql_mode_t* pSql_mode)
{
    *pSql_mode = this_thread.sql_mode;
    return QC_RESULT_OK;
}

int32_t qc_sqlite_set_sql_mode(qc_sql_mode_t sql_mode)
{
    int32_t rv = QC_RESULT_OK;

    switch (sql_mode)
    {
    case QC_SQL_MODE_DEFAULT:
        this_thread.sql_mode = sql_mode;
        if (this_unit.parse_as == QC_PARSE_AS_103)
        {
            this_thread.pFunction_name_mappings = function_name_mappings_103;
        }
        else
        {
            this_thread.pFunction_name_mappings = function_name_mappings_default;
        }
        break;

    case QC_SQL_MODE_ORACLE:
        this_thread.sql_mode = sql_mode;
        this_thread.pFunction_name_mappings = function_name_mappings_oracle;
        break;

    default:
        rv = QC_RESULT_ERROR;
    }

    return rv;
}

/**
 * EXPORTS
 */

extern "C"
{

MXS_MODULE* MXS_CREATE_MODULE()
{
    static QUERY_CLASSIFIER qc =
    {
        qc_sqlite_setup,
        qc_sqlite_process_init,
        qc_sqlite_process_end,
        qc_sqlite_thread_init,
        qc_sqlite_thread_end,
        qc_sqlite_parse,
        qc_sqlite_get_type_mask,
        qc_sqlite_get_operation,
        qc_sqlite_get_created_table_name,
        qc_sqlite_is_drop_table_query,
        qc_sqlite_get_table_names,
        NULL,
        qc_sqlite_query_has_clause,
        qc_sqlite_get_database_names,
        qc_sqlite_get_prepare_name,
        qc_sqlite_get_field_info,
        qc_sqlite_get_function_info,
        qc_sqlite_get_preparable_stmt,
        qc_sqlite_set_server_version,
        qc_sqlite_get_server_version,
        qc_sqlite_get_sql_mode,
        qc_sqlite_set_sql_mode,
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_QUERY_CLASSIFIER,
        MXS_MODULE_BETA_RELEASE,
        QUERY_CLASSIFIER_VERSION,
        "Query classifier using sqlite.",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &qc,
        qc_sqlite_process_init,
        qc_sqlite_process_end,
        qc_sqlite_thread_init,
        qc_sqlite_thread_end,
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

}

