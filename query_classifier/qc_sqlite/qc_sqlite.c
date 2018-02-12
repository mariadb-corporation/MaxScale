/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <sqliteInt.h>

#include <signal.h>
#include <string.h>
#include <log_manager.h>
#include <modinfo.h>
#include <mysql_client_server_protocol.h>
#include <platform.h>
#include <query_classifier.h>
#include <skygw_utils.h>
#include <modutil.h>
#include "builtin_functions.h"

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

static inline bool qc_info_is_valid(qc_parse_result_t status)
{
    return status != QC_QUERY_INVALID;
}

static inline bool qc_info_was_tokenized(qc_parse_result_t status)
{
    return status == QC_QUERY_TOKENIZED;
}

static inline bool qc_info_was_parsed(qc_parse_result_t status)
{
    return status == QC_QUERY_PARSED;
}

/**
 * Contains information about a particular query.
 */
typedef struct qc_sqlite_info
{
    qc_parse_result_t status;        // The validity of the information in this structure.
    const char* query;               // The query passed to sqlite.
    size_t query_len;                // The length of the query.

    uint32_t types;                  // The types of the query.
    qc_query_op_t operation;         // The operation in question.
    char* affected_fields;           // The affected fields.
    size_t affected_fields_len;      // The used length of affected_fields.
    size_t affected_fields_capacity; // The capacity of affected_fields.
    bool is_real_query;              // SELECT, UPDATE, INSERT, DELETE or a variation.
    bool has_clause;                 // Has WHERE or HAVING.
    char** table_names;              // Array of table names used in the query.
    size_t table_names_len;          // The used entries in table_names.
    size_t table_names_capacity;     // The capacity of table_names.
    char** table_fullnames;          // Array of full (i.e. qualified) table names used in the query.
    size_t table_fullnames_len;      // The used entries in table_fullnames.
    size_t table_fullnames_capacity; // The capacity of table_fullnames.
    char* created_table_name;        // The name of a created table.
    bool is_drop_table;              // Is the query a DROP TABLE.
    char** database_names;           // Array of database names used in the query.
    size_t database_names_len;       // The used entries in database_names.
    size_t database_names_capacity;  // The capacity of database_names.
    int keyword_1;                   // The first encountered keyword.
    int keyword_2;                   // The second encountered keyword.
    bool initializing;               // Whether we are initializing sqlite3.
} QC_SQLITE_INFO;

typedef enum qc_log_level
{
    QC_LOG_NOTHING = 0,
    QC_LOG_NON_PARSED,
    QC_LOG_NON_PARTIALLY_PARSED,
    QC_LOG_NON_TOKENIZED,
} qc_log_level_t;


/**
 * The state of qc_sqlite.
 */
static struct
{
    bool initialized;
    qc_log_level_t log_level;
} this_unit;

/**
 * The qc_sqlite thread-specific state.
 */
static thread_local struct
{
    bool initialized;
    sqlite3* db;      // Thread specific database handle.
    QC_SQLITE_INFO* info;
} this_thread;


/**
 * MaxScale allocation functions.
 *
 * In the environments where MaxScale runs, malloc and friends will not fail, but
 * in low-memory situations some process will be killed. These functions now
 * never return NULL, but abort the process should the allocation failed for some
 * unknown reason.
 */
static void* mxs_malloc(size_t size)
{
    void* p = malloc(size);
    if (!p)
    {
        raise(SIGABRT);
    }

    return p;
}

static void mxs_free(void* p)
{
    free(p);
}

static void* mxs_realloc(void* p, size_t size)
{
    p = realloc(p, size);
    if (!p)
    {
        raise(SIGABRT);
    }

    return p;
}

static void* mxs_calloc(size_t n, size_t size)
{
    void* p = calloc(n, size);
    if (!p)
    {
        raise(SIGABRT);
    }

    return p;
}

static char* mxs_strdup(const char* s1)
{
    char* s2 = strdup(s1);
    if (!s2)
    {
        raise(SIGABRT);
    }

    return s2;
}


/**
 * HELPERS
 */

typedef enum qc_token_position
{
    QC_TOKEN_MIDDLE, // In the middle or irrelevant, e.g.: "=" in "a = b".
    QC_TOKEN_LEFT,   // To the left, e.g.: "a" in "a = b".
    QC_TOKEN_RIGHT,  // To the right, e.g: "b" in "a = b".
} qc_token_position_t;

static void append_affected_field(QC_SQLITE_INFO* info, const char* s);
static void buffer_object_free(void* data);
static char** copy_string_array(char** strings, int* pn);
static void enlarge_string_array(size_t n, size_t len, char*** ppzStrings, size_t* pCapacity);
static bool ensure_query_is_parsed(GWBUF* query);
static void free_string_array(char** sa);
static QC_SQLITE_INFO* get_query_info(GWBUF* query);
static QC_SQLITE_INFO* info_alloc(void);
static void info_finish(QC_SQLITE_INFO* info);
static void info_free(QC_SQLITE_INFO* info);
static QC_SQLITE_INFO* info_init(QC_SQLITE_INFO* info);
static void log_invalid_data(GWBUF* query, const char* message);
static bool parse_query(GWBUF* query);
static void parse_query_string(const char* query, size_t len);
static bool query_is_parsed(GWBUF* query);
static bool should_exclude(const char* zName, const ExprList* pExclude);
static void update_affected_fields(QC_SQLITE_INFO* info,
                                   int prev_token,
                                   const Expr* pExpr,
                                   qc_token_position_t pos,
                                   const ExprList* pExclude);
static void update_affected_fields_from_exprlist(QC_SQLITE_INFO* info,
                                                 const ExprList* pEList, const ExprList* pExclude);
static void update_affected_fields_from_idlist(QC_SQLITE_INFO* info,
                                               const IdList* pIds, const ExprList* pExclude);
static void update_affected_fields_from_select(QC_SQLITE_INFO* info,
                                               const Select* pSelect, const ExprList* pExclude);
static void update_database_names(QC_SQLITE_INFO* info, const char* name);
static void update_names(QC_SQLITE_INFO* info, const char* zDatabase, const char* zTable);
static void update_names_from_srclist(QC_SQLITE_INFO* info, const SrcList* pSrc);

// Defined in parse.y
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
extern void maxscaleCollectInfoFromSelect(Parse*, Select*);

/**
 * Used for freeing a QC_SQLITE_INFO object added to a GWBUF.
 *
 * @param object A pointer to a QC_SQLITE_INFO object.
 */
static void buffer_object_free(void* data)
{
    info_free((QC_SQLITE_INFO*) data);
}

static char** copy_string_array(char** strings, int* pn)
{
    size_t n = 0;

    char** ss = strings;
    *pn = 0;

    while (*ss)
    {
        ++ss;
        ++(*pn);
    }

    ss = (char**) mxs_malloc((*pn + 1) * sizeof(char*));

    ss[*pn] = 0;

    for (int i = 0; i < *pn; ++i)
    {
        ss[i] = mxs_strdup(strings[i]);
    }

    return ss;
}

static void enlarge_string_array(size_t n, size_t len, char*** ppzStrings, size_t* pCapacity)
{
    if (len + n >= *pCapacity)
    {
        int capacity = *pCapacity ? *pCapacity * 2 : 4;

        *ppzStrings = (char**) mxs_realloc(*ppzStrings, capacity * sizeof(char**));
        *pCapacity = capacity;
    }
}

static bool ensure_query_is_parsed(GWBUF* query)
{
    bool parsed = query_is_parsed(query);

    if (!parsed)
    {
        parsed = parse_query(query);
    }

    return parsed;
}

static void free_string_array(char** sa)
{
    if (sa)
    {
        char** s = sa;

        while (*s)
        {
            free(*s);
            ++s;
        }

        free(sa);
    }
}

static QC_SQLITE_INFO* get_query_info(GWBUF* query)
{
    QC_SQLITE_INFO* info = NULL;

    if (ensure_query_is_parsed(query))
    {
        info = (QC_SQLITE_INFO*) gwbuf_get_buffer_object_data(query, GWBUF_PARSING_INFO);
        ss_dassert(info);
    }

    return info;
}

static QC_SQLITE_INFO* info_alloc(void)
{
    QC_SQLITE_INFO* info = mxs_malloc(sizeof(*info));

    info_init(info);

    return info;
}

static void info_finish(QC_SQLITE_INFO* info)
{
    free(info->affected_fields);
    free_string_array(info->table_names);
    free_string_array(info->table_fullnames);
    free(info->created_table_name);
    free_string_array(info->database_names);
}

static void info_free(QC_SQLITE_INFO* info)
{
    if (info)
    {
        info_finish(info);
        free(info);
    }
}

static QC_SQLITE_INFO* info_init(QC_SQLITE_INFO* info)
{
    memset(info, 0, sizeof(*info));

    info->status = QC_QUERY_INVALID;

    info->types = QUERY_TYPE_UNKNOWN;
    info->operation = QUERY_OP_UNDEFINED;
    info->affected_fields = NULL;
    info->affected_fields_len = 0;
    info->affected_fields_capacity = 0;
    info->is_real_query = false;
    info->has_clause = false;
    info->table_names = NULL;
    info->table_names_len = 0;
    info->table_names_capacity = 0;
    info->table_fullnames = NULL;
    info->table_fullnames_len = 0;
    info->table_fullnames_capacity = 0;
    info->created_table_name = NULL;
    info->is_drop_table = false;
    info->database_names = NULL;
    info->database_names_len = 0;
    info->database_names_capacity = 0;
    info->keyword_1 = 0; // Sqlite3 starts numbering tokens from 1, so 0 means
    info->keyword_2 = 0; // that we have not seen a keyword.
    info->initializing = false;

    return info;
}

static void parse_query_string(const char* query, size_t len)
{
    sqlite3_stmt* stmt = NULL;
    const char* tail = NULL;

    ss_dassert(this_thread.db);
    int rc = sqlite3_prepare(this_thread.db, query, len, &stmt, &tail);

    const int max_len = 512; // Maximum length of logged statement.
    const int l = (len > max_len ? max_len : len);
    const char* suffix = (len > max_len ? "..." : "");
    const char* format;

    if (rc != SQLITE_OK)
    {
        if (qc_info_was_tokenized(this_thread.info->status))
        {
            format =
                "qc_sqlite: Statement was classified only based on keywords "
                "(Sqlite3 error: %s, %s): \"%.*s%s\"";
        }
        else
        {
            if (qc_info_was_parsed(this_thread.info->status))
            {
                format =
                    "qc_sqlite: Statement was only partially parsed "
                    "(Sqlite3 error: %s, %s): \"%.*s%s\"";

                // The status was set to QC_QUERY_PARSED, but sqlite3 returned an
                // error. Most likely, query contains some excess unrecognized stuff.
                this_thread.info->status = QC_QUERY_PARTIALLY_PARSED;
            }
            else
            {
                format =
                    "qc_sqlite: Statement was neither parsed nor recognized from keywords "
                    "(Sqlite3 error: %s, %s): \"%.*s%s\"";
            }
        }

        if (this_unit.log_level > QC_LOG_NOTHING)
        {
            bool log_warning = false;

            switch (this_unit.log_level)
            {
            case QC_LOG_NON_PARSED:
                log_warning = this_thread.info->status < QC_QUERY_PARSED;
                break;

            case QC_LOG_NON_PARTIALLY_PARSED:
                log_warning = this_thread.info->status < QC_QUERY_PARTIALLY_PARSED;
                break;

            case QC_LOG_NON_TOKENIZED:
                log_warning = this_thread.info->status < QC_QUERY_TOKENIZED;
                break;

            default:
                ss_dassert(!true);
                break;
            }

            if (log_warning)
            {
                MXS_WARNING(format, sqlite3_errstr(rc), sqlite3_errmsg(this_thread.db), l, query, suffix);
            }
        }
    }
    else
    {
        if (qc_info_was_tokenized(this_thread.info->status))
        {
            // This suggests a callback from the parser into this module is not made.
            format =
                "qc_sqlite: Statement was classified only based on keywords, "
                "even though the statement was parsed: \"%.*s%s\"";

            MXS_WARNING(format, l, query, suffix);
        }
        else if (!qc_info_was_parsed(this_thread.info->status))
        {
            // This suggests there are keywords that should be recognized but are not,
            // a tentative classification cannot be (or is not) made using the keywords
            // seen and/or a callback from the parser into this module is not made.
            format = "qc_sqlite: Statement was parsed, but not classified: \"%.*s%s\"";

            MXS_ERROR(format, l, query, suffix);
        }
    }

    if (stmt)
    {
        sqlite3_finalize(stmt);
    }
}

static bool parse_query(GWBUF* query)
{
    bool parsed = false;
    ss_dassert(!query_is_parsed(query));

    QC_SQLITE_INFO* info = info_alloc();

    if (info)
    {
        this_thread.info = info;

        // TODO: Somewhere it needs to be ensured that this buffer is contiguous.
        // TODO: Where is it checked that the GWBUF really contains a query?
        uint8_t* data = (uint8_t*) GWBUF_DATA(query);
        size_t len = MYSQL_GET_PACKET_LEN(data) - 1; // Subtract 1 for packet type byte.

        const char* s = (const char*) &data[5]; // TODO: Are there symbolic constants somewhere?

        this_thread.info->query = s;
        this_thread.info->query_len = len;
        parse_query_string(s, len);
        this_thread.info->query = NULL;
        this_thread.info->query_len = 0;

        // TODO: Add return value to gwbuf_add_buffer_object.
        // Always added; also when it was not recognized. If it was not recognized now,
        // it won't be if we try a second time.
        gwbuf_add_buffer_object(query, GWBUF_PARSING_INFO, info, buffer_object_free);
        parsed = true;

        this_thread.info = NULL;
    }
    else
    {
        MXS_ERROR("qc_sqlite: Could not allocate structure for containing parse data.");
    }

    return parsed;
}

static bool query_is_parsed(GWBUF* query)
{
    return query && GWBUF_IS_PARSED(query);
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
            if (length > GWBUF_LENGTH(query) - MYSQL_HEADER_LEN - 1)
            {
                length = GWBUF_LENGTH(query) - MYSQL_HEADER_LEN - 1;
            }

            MXS_INFO("qc_sqlite: Parsing the query failed, %s: %*s", message, length, sql);
        }
    }
}

static void append_affected_field(QC_SQLITE_INFO* info, const char* s)
{
    size_t len = strlen(s);
    size_t required_len = info->affected_fields_len + len + 1; // 1 for NULL

    if (info->affected_fields_len != 0)
    {
        required_len += 1; // " " between fields
    }

    if (required_len > info->affected_fields_capacity)
    {
        if (info->affected_fields_capacity == 0)
        {
            info->affected_fields_capacity = 32;
        }

        while (required_len > info->affected_fields_capacity)
        {
            info->affected_fields_capacity *= 2;
        }

        info->affected_fields = mxs_realloc(info->affected_fields, info->affected_fields_capacity);
    }

    if (info->affected_fields_len != 0)
    {
        strcpy(info->affected_fields + info->affected_fields_len, " ");
        info->affected_fields_len += 1;
    }

    strcpy(info->affected_fields + info->affected_fields_len, s);
    info->affected_fields_len += len;
}

static bool should_exclude(const char* zName, const ExprList* pExclude)
{
    int i;
    for (i = 0; i < pExclude->nExpr; ++i)
    {
        const struct ExprList_item* item = &pExclude->a[i];

        // zName will contain a possible alias name. If the alias name
        // is referred to in e.g. in a having, it need to be excluded
        // from the affected fields. It's not a real field.
        if (item->zName && (strcasecmp(item->zName, zName) == 0))
        {
            break;
        }

        Expr* pExpr = item->pExpr;

        if (pExpr->op == TK_DOT)
        {
            pExpr = pExpr->pRight;
        }

        // We need to ensure that we do not report fields where there
        // is only a difference in case. E.g.
        //     SELECT A FROM tbl WHERE a = "foo";
        // Affected fields is "A" and not "A a".
        if ((pExpr->op == TK_ID) && (strcasecmp(pExpr->u.zToken, zName) == 0))
        {
            break;
        }
    }

    return i != pExclude->nExpr;
}

static void update_affected_fields(QC_SQLITE_INFO* info,
                                   int prev_token,
                                   const Expr* pExpr,
                                   qc_token_position_t pos,
                                   const ExprList* pExclude)
{
    const char* zToken = pExpr->u.zToken;

    switch (pExpr->op)
    {
    case TK_ASTERISK: // "select *"
        append_affected_field(info, "*");
        break;

    case TK_DOT:
        // In case of "X.Y" qc_mysqlembedded returns "Y".
        update_affected_fields(info, TK_DOT, pExpr->pRight, QC_TOKEN_RIGHT, pExclude);
        break;

    case TK_ID:
        if ((pExpr->flags & EP_DblQuoted) == 0)
        {
            if ((strcasecmp(zToken, "true") != 0) && (strcasecmp(zToken, "false") != 0))
            {
                if (!pExclude || !should_exclude(zToken, pExclude))
                {
                    append_affected_field(info, zToken);
                }
            }
        }
        break;

    case TK_VARIABLE:
        {
            if (zToken[0] == '@')
            {
                if (zToken[1] == '@')
                {
                    if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT))
                    {
                        info->types |= QUERY_TYPE_GSYSVAR_WRITE;
                    }
                    else
                    {
                        if ((strcasecmp(&zToken[2], "identity") == 0) ||
                            (strcasecmp(&zToken[2], "last_insert_id") == 0))
                        {
                            info->types |= QUERY_TYPE_MASTER_READ;
                        }
                        else
                        {
                            info->types |= QUERY_TYPE_SYSVAR_READ;
                        }
                    }
                }
                else
                {
                    if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT))
                    {
                        info->types |= QUERY_TYPE_USERVAR_WRITE;
                    }
                    else
                    {
                        info->types |= QUERY_TYPE_USERVAR_READ;
                    }
                }
            }
            else if (zToken[0] != '?')
            {
                MXS_WARNING("qc_sqlite: %s reported as VARIABLE.", zToken);
            }
        }
        break;

    default:
        MXS_DEBUG("qc_sqlite: Token %d not handled explicitly.", pExpr->op);
        // Fallthrough intended.
    case TK_BETWEEN:
    case TK_CASE:
    case TK_EXISTS:
    case TK_FUNCTION:
    case TK_IN:
    case TK_SELECT:
        if ((pExpr->op == TK_FUNCTION) && zToken)
        {
            if (strcasecmp(zToken, "last_insert_id") == 0)
            {
                info->types |= (QUERY_TYPE_READ | QUERY_TYPE_MASTER_READ);
            }
            else if (!is_builtin_readonly_function(zToken))
            {
                info->types |= QUERY_TYPE_WRITE;
            }
        }

        if (pExpr->pLeft)
        {
            update_affected_fields(info, pExpr->op, pExpr->pLeft, QC_TOKEN_LEFT, pExclude);
        }

        if (pExpr->pRight)
        {
            update_affected_fields(info, pExpr->op, pExpr->pRight, QC_TOKEN_RIGHT, pExclude);
        }

        if (pExpr->x.pList)
        {
            switch (pExpr->op)
            {
            case TK_BETWEEN:
            case TK_CASE:
            case TK_FUNCTION:
                update_affected_fields_from_exprlist(info, pExpr->x.pList, pExclude);
                break;

            case TK_EXISTS:
            case TK_IN:
            case TK_SELECT:
                if (pExpr->flags & EP_xIsSelect)
                {
                    update_affected_fields_from_select(info, pExpr->x.pSelect, pExclude);
                }
                else
                {
                    update_affected_fields_from_exprlist(info, pExpr->x.pList, pExclude);
                }
                break;
            }
        }
        break;
    }
}

static void update_affected_fields_from_exprlist(QC_SQLITE_INFO* info,
                                               const ExprList* pEList,
                                               const ExprList* pExclude)
{
    for (int i = 0; i < pEList->nExpr; ++i)
    {
        struct ExprList_item* pItem = &pEList->a[i];

        update_affected_fields(info, 0, pItem->pExpr, QC_TOKEN_MIDDLE, pExclude);
    }
}

static void update_affected_fields_from_idlist(QC_SQLITE_INFO* info,
                                               const IdList* pIds,
                                               const ExprList* pExclude)
{
    for (int i = 0; i < pIds->nId; ++i)
    {
        struct IdList_item* pItem = &pIds->a[i];

        if (!pExclude || !should_exclude(pItem->zName, pExclude))
        {
            append_affected_field(info, pItem->zName);
        }
    }
}

static void update_affected_fields_from_select(QC_SQLITE_INFO* info,
                                               const Select* pSelect,
                                               const ExprList* pExclude)
{
    if (pSelect->pSrc)
    {
        const SrcList* pSrc = pSelect->pSrc;

        for (int i = 0; i < pSrc->nSrc; ++i)
        {
            if (pSrc->a[i].zName)
            {
                update_names(info, pSrc->a[i].zDatabase, pSrc->a[i].zName);
                info->is_real_query = true;
            }

            if (pSrc->a[i].pSelect)
            {
                update_affected_fields_from_select(info, pSrc->a[i].pSelect, pExclude);
            }

#ifdef QC_COLLECT_NAMES_FROM_USING
            // With this enabled, the affected fields of
            //    select * from (t1 as t2 left join t1 as t3 using (a)), t1;
            // will be "* a", otherwise "*". However, that "a" is used in the join
            // does not reveal its value, right?
            if (pSrc->a[i].pUsing)
            {
                update_affected_fields_from_idlist(info, pSrc->a[i].pUsing, pSelect->pEList);
            }
#endif
        }
    }

    if (pSelect->pEList)
    {
        update_affected_fields_from_exprlist(info, pSelect->pEList, NULL);
    }

    if (pSelect->pWhere)
    {
        info->has_clause = true;
        update_affected_fields(info, 0, pSelect->pWhere, QC_TOKEN_MIDDLE, pSelect->pEList);
    }

    if (pSelect->pGroupBy)
    {
        update_affected_fields_from_exprlist(info, pSelect->pGroupBy, pSelect->pEList);
    }

    if (pSelect->pHaving)
    {
        info->has_clause = true;
        update_affected_fields(info, 0, pSelect->pHaving, QC_TOKEN_MIDDLE, pSelect->pEList);
    }
}

static void update_database_names(QC_SQLITE_INFO* info, const char* zDatabase)
{
    char* zCopy = mxs_strdup(zDatabase);
    exposed_sqlite3Dequote(zCopy);

    enlarge_string_array(1, info->database_names_len,
                         &info->database_names, &info->database_names_capacity);
    info->database_names[info->database_names_len++] = zCopy;
    info->database_names[info->database_names_len] = NULL;
}

static void update_names(QC_SQLITE_INFO* info, const char* zDatabase, const char* zTable)
{
    char* zCopy = mxs_strdup(zTable);
    // TODO: Is this call really needed. Check also sqlite3Dequote.
    exposed_sqlite3Dequote(zCopy);

    enlarge_string_array(1, info->table_names_len, &info->table_names, &info->table_names_capacity);
    info->table_names[info->table_names_len++] = zCopy;
    info->table_names[info->table_names_len] = NULL;

    if (zDatabase)
    {
        zCopy = mxs_malloc(strlen(zDatabase) + 1 + strlen(zTable) + 1);

        strcpy(zCopy, zDatabase);
        strcat(zCopy, ".");
        strcat(zCopy, zTable);
        exposed_sqlite3Dequote(zCopy);

        update_database_names(info, zDatabase);
    }
    else
    {
        zCopy = mxs_strdup(zCopy);
    }

    enlarge_string_array(1, info->table_fullnames_len,
                         &info->table_fullnames, &info->table_fullnames_capacity);
    info->table_fullnames[info->table_fullnames_len++] = zCopy;
    info->table_fullnames[info->table_fullnames_len] = NULL;
}

static void update_names_from_srclist(QC_SQLITE_INFO* info, const SrcList* pSrc)
{
    for (int i = 0; i < pSrc->nSrc; ++i)
    {
        if (pSrc->a[i].zName)
        {
            update_names(info, pSrc->a[i].zDatabase, pSrc->a[i].zName);
        }

        if (pSrc->a[i].pSelect && pSrc->a[i].pSelect->pSrc)
        {
            update_names_from_srclist(info, pSrc->a[i].pSelect->pSrc);
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

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_ALTER;
}

void mxs_sqlite3AlterBeginAddColumn(Parse* pParse, SrcList* pSrcList)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    update_names_from_srclist(info, pSrcList);

    exposed_sqlite3SrcListDelete(pParse->db, pSrcList);
}

void mxs_sqlite3Analyze(Parse* pParse, SrcList* pSrcList)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

    update_names_from_srclist(info, pSrcList);

    exposed_sqlite3SrcListDelete(pParse->db, pSrcList);
}

void mxs_sqlite3BeginTransaction(Parse* pParse, int type)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_BEGIN_TRX;
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

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

    if (pTableName)
    {
        for (int i = 0; i < pTableName->nAlloc; ++i)
        {
            struct SrcList_item* pItem = &pTableName->a[i];

            if (pItem->zName)
            {
                update_names(info, pItem->zDatabase, pItem->zName);
            }
        }
    }

    // We need to call this, otherwise finish trigger will not be called.
    exposed_sqlite3BeginTrigger(pParse, pName1, pName2, tr_tm, op, pColumns,
                                pTableName, pWhen, isTemp, noErr);
}

void mxs_sqlite3CommitTransaction(Parse* pParse)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_COMMIT;
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

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_CREATE;

    if (pTblName)
    {
        update_names_from_srclist(info, pTblName);
    }
    else if (pParse->pNewTable)
    {
        update_names(info, NULL, pParse->pNewTable->zName);
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
    QC_TRACE();
    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_CREATE;

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

        update_names(info, database, name);
    }
    else
    {
        update_names(info, NULL, name);
    }

    if (pSelect)
    {
        update_affected_fields_from_select(info, pSelect, NULL);
        info->is_real_query = false;
    }

    exposed_sqlite3ExprListDelete(pParse->db, pCNames);
    // pSelect is deleted in parse.y
}

void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_DELETE;
    info->is_real_query = true;
    info->has_clause = pWhere ? true : false;

    if (pUsing)
    {
        // Walk through the using declaration and update
        // table and database names.
        for (int i = 0; i < pUsing->nSrc; ++i)
        {
            struct SrcList_item* pItem = &pUsing->a[i];

            update_names(info, pItem->zDatabase, pItem->zName);
        }

        // Walk through the tablenames while excluding alias
        // names from the using declaration.
        for (int i = 0; i < pTabList->nSrc; ++i)
        {
            const struct SrcList_item* pTable = &pTabList->a[i];
            ss_dassert(pTable->zName);
            int j = 0;
            bool isSame = false;

            do
            {
                struct SrcList_item* pItem = &pUsing->a[j++];

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
                update_names(info, pTable->zDatabase, pTable->zName);
            }
        }
    }
    else
    {
        update_names_from_srclist(info, pTabList);
    }

    if (pWhere)
    {
        update_affected_fields(info, 0, pWhere, QC_TOKEN_MIDDLE, 0);
    }

    exposed_sqlite3ExprDelete(pParse->db, pWhere);
    exposed_sqlite3SrcListDelete(pParse->db, pTabList);
    exposed_sqlite3SrcListDelete(pParse->db, pUsing);
}

void mxs_sqlite3DropIndex(Parse* pParse, SrcList* pName, SrcList* pTable, int bits)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_DROP;

    update_names_from_srclist(info, pTable);

    exposed_sqlite3SrcListDelete(pParse->db, pName);
    exposed_sqlite3SrcListDelete(pParse->db, pTable);
}

void mxs_sqlite3DropTable(Parse *pParse, SrcList *pName, int isView, int noErr, int isTemp)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
    if (!isTemp)
    {
        info->types |= QUERY_TYPE_COMMIT;
    }
    info->operation = QUERY_OP_DROP;
    if (!isView)
    {
        info->is_drop_table = true;
    }
    update_names_from_srclist(info, pName);

    exposed_sqlite3SrcListDelete(pParse->db, pName);
}

void mxs_sqlite3EndTable(Parse *pParse,    /* Parse context */
                         Token *pCons,     /* The ',' token after the last column defn. */
                         Token *pEnd,      /* The ')' before options in the CREATE TABLE */
                         u8 tabOpts,       /* Extra table options. Usually 0. */
                         Select *pSelect,  /* Select from a "CREATE ... AS SELECT" */
                         SrcList* pOldTable) /* The old table in "CREATE ... LIKE OldTable" */
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    if (!info->initializing)
    {
        if (pSelect)
        {
            update_affected_fields_from_select(info, pSelect, NULL);
            info->is_real_query = false;
        }
        else if (pOldTable)
        {
            update_names_from_srclist(info, pOldTable);
            exposed_sqlite3SrcListDelete(pParse->db, pOldTable);
        }

        // pSelect is deleted in parse.y
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

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_INSERT;
    info->is_real_query = true;
    ss_dassert(pTabList);
    ss_dassert(pTabList->nSrc >= 1);
    update_names_from_srclist(info, pTabList);

    if (pColumns)
    {
        update_affected_fields_from_idlist(info, pColumns, NULL);
    }

    if (pSelect)
    {
        update_affected_fields_from_select(info, pSelect, NULL);
    }

    if (pSet)
    {
        update_affected_fields_from_exprlist(info, pSet, NULL);
    }

    exposed_sqlite3SrcListDelete(pParse->db, pTabList);
    exposed_sqlite3IdListDelete(pParse->db, pColumns);
    exposed_sqlite3ExprListDelete(pParse->db, pSet);
    // pSelect is deleted in parse.y
}

void mxs_sqlite3RollbackTransaction(Parse* pParse)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_ROLLBACK;
}

int mxs_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest)
{
    int rc = -1;
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    if (!info->initializing)
    {
        info->status = QC_QUERY_PARSED;
        info->operation = QUERY_OP_SELECT;

        maxscaleCollectInfoFromSelect(pParse, p);
        // NOTE: By convention, the select is deleted in parse.y.
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

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    if (!info->initializing)
    {
        info->status = QC_QUERY_PARSED;
        info->operation = QUERY_OP_CREATE;
        info->types = QUERY_TYPE_WRITE;

        if (isTemp)
        {
            info->types |= QUERY_TYPE_CREATE_TMP_TABLE;
        }
        else
        {
            info->types |= QUERY_TYPE_COMMIT;
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

            update_names(info, database, name);
        }
        else
        {
            update_names(info, NULL, name);
        }

        info->created_table_name = mxs_strdup(info->table_names[0]);
    }
    else
    {
        exposed_sqlite3StartTable(pParse, pName1, pName2, isTemp, isView, isVirtual, noErr);
    }
}

void mxs_sqlite3Update(Parse* pParse, SrcList* pTabList, ExprList* pChanges, Expr* pWhere, int onError)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_UPDATE;
    info->is_real_query = true;
    update_names_from_srclist(info, pTabList);
    info->has_clause = (pWhere ? true : false);

    if (pChanges)
    {
        for (int i = 0; i < pChanges->nExpr; ++i)
        {
            struct ExprList_item* pItem = &pChanges->a[i];

            if (pItem->zName)
            {
                append_affected_field(info, pItem->zName);
            }

            update_affected_fields(info, 0, pItem->pExpr, QC_TOKEN_MIDDLE, NULL);
        }
    }

    if (pWhere)
    {
        update_affected_fields(info, 0, pWhere, QC_TOKEN_MIDDLE, NULL);
    }

    exposed_sqlite3SrcListDelete(pParse->db, pTabList);
    exposed_sqlite3ExprListDelete(pParse->db, pChanges);
    exposed_sqlite3ExprDelete(pParse->db, pWhere);
}

void mxs_sqlite3Savepoint(Parse *pParse, int op, Token *pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
}

void maxscaleCollectInfoFromSelect(Parse* pParse, Select* pSelect)
{
    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    if (pSelect->pInto)
    {
        // If there's a single variable, then it's a write.
        // mysql embedded considers it a system var write.
        info->types = QUERY_TYPE_GSYSVAR_WRITE;

        // Also INTO {OUTFILE|DUMPFILE} will be typed as QUERY_TYPE_GSYSVAR_WRITE.
    }
    else
    {
        info->types = QUERY_TYPE_READ;
    }

    update_affected_fields_from_select(info, pSelect, NULL);
}

void maxscaleAlterTable(Parse *pParse,            /* Parser context. */
                        mxs_alter_t command,
                        SrcList *pSrc,            /* The table to rename. */
                        Token *pName)             /* The new table name (RENAME). */
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_ALTER;

    switch (command)
    {
    case MXS_ALTER_DISABLE_KEYS:
        update_names_from_srclist(info, pSrc);
        break;

    case MXS_ALTER_ENABLE_KEYS:
        update_names_from_srclist(info, pSrc);
        break;

    case MXS_ALTER_RENAME:
        update_names_from_srclist(info, pSrc);
        break;

    default:
        ;
    }

    exposed_sqlite3SrcListDelete(pParse->db, pSrc);
}

void maxscaleCall(Parse* pParse, SrcList* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;

    exposed_sqlite3SrcListDelete(pParse->db, pName);
}

void maxscaleCheckTable(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

    update_names_from_srclist(info, pTables);

    exposed_sqlite3SrcListDelete(pParse->db, pTables);
}

void maxscaleComment()
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    if (info->status == QC_QUERY_INVALID)
    {
        info->status = QC_QUERY_PARSED;
        info->types = QUERY_TYPE_READ;
    }
}

void maxscaleDeallocate(Parse* pParse, Token* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
}

void maxscaleDo(Parse* pParse, ExprList* pEList)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_READ | QUERY_TYPE_WRITE);

    exposed_sqlite3ExprListDelete(pParse->db, pEList);
}

void maxscaleDrop(Parse* pParse, MxsDrop* pDrop)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_DROP;
}

void maxscaleExecute(Parse* pParse, Token* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
    info->is_real_query = true;
}

void maxscaleExplain(Parse* pParse, SrcList* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_READ;
    update_names(info, pName->a[0].zDatabase, pName->a[0].zName);
    append_affected_field(info,
                          "COLUMN_DEFAULT COLUMN_KEY COLUMN_NAME "
                          "COLUMN_TYPE EXTRA IS_NULLABLE");

    exposed_sqlite3SrcListDelete(pParse->db, pName);
}

void maxscaleFlush(Parse* pParse, Token* pWhat)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
}

void maxscaleHandler(Parse* pParse, mxs_handler_t type, SrcList* pFullName, Token* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;

    switch (type)
    {
    case MXS_HANDLER_OPEN:
        {
            info->types = QUERY_TYPE_WRITE;

            ss_dassert(pFullName->nSrc == 1);
            const struct SrcList_item* pItem = &pFullName->a[0];

            update_names(info, pItem->zDatabase, pItem->zName);
        }
        break;

    case MXS_HANDLER_CLOSE:
        {
            info->types = QUERY_TYPE_WRITE;

            char zName[pName->n + 1];
            strncpy(zName, pName->z, pName->n);
            zName[pName->n] = 0;

            update_names(info, "*any*", zName);
        }
        break;

    default:
        ss_dassert(!true);
    }

    exposed_sqlite3SrcListDelete(pParse->db, pFullName);
}

void maxscaleLoadData(Parse* pParse, SrcList* pFullName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_LOAD;

    if (pFullName)
    {
        update_names_from_srclist(info, pFullName);

        exposed_sqlite3SrcListDelete(pParse->db, pFullName);
    }
}

void maxscaleLock(Parse* pParse, mxs_lock_t type, SrcList* pTables)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE;

    if (pTables)
    {
        update_names_from_srclist(info, pTables);

        exposed_sqlite3SrcListDelete(pParse->db, pTables);
    }
}

void maxscaleKeyword(int token)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    // This function is called for every keyword the sqlite3 parser encounters.
    // We will store in info->keyword_{1|2} the first and second keyword that
    // are encountered, and when they _are_ encountered, we make an educated
    // deduction about the statement. We can make that deduction only the first
    // (and second) time we see a keyword, so that we don't get confused by a
    // statement like "CREATE TABLE ... AS SELECT ...".
    // Since info->keyword_{1|2} is initialized with 0, well, if it is 0 then
    // we have not seen the {1st|2nd} keyword yet.

    if (!info->keyword_1)
    {
        info->keyword_1 = token;

        switch (info->keyword_1)
        {
        case TK_ALTER:
            info->status = QC_QUERY_TOKENIZED;
            info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_ALTER;
            break;

        case TK_CALL:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            break;

        case TK_CREATE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_CREATE;
            break;

        case TK_DELETE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_DELETE;
            info->is_real_query = true;
            break;

        case TK_DESC:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_READ;
            break;

        case TK_DROP:
            info->status = QC_QUERY_TOKENIZED;
            info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_DROP;
            break;

        case TK_EXECUTE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            info->is_real_query = true;
            break;

        case TK_EXPLAIN:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_READ;
            break;

        case TK_GRANT:
            info->status = QC_QUERY_TOKENIZED;
            info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_GRANT;
            break;

        case TK_HANDLER:
            info->status = QUERY_TYPE_WRITE;
            break;

        case TK_INSERT:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_INSERT;
            info->is_real_query = true;
            break;

        case TK_LOCK:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            break;

        case TK_PREPARE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_PREPARE_NAMED_STMT;
            info->is_real_query = true;
            break;

        case TK_REPLACE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_INSERT;
            info->is_real_query = true;
            break;

        case TK_REVOKE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_REVOKE;
            break;

        case TK_SELECT:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_READ;
            info->operation = QUERY_OP_SELECT;
            break;

        case TK_SET:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_GSYSVAR_WRITE;
            break;

        case TK_SHOW:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            break;

        case TK_START:
            // Will produce the right info for START SLAVE.
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            break;

        case TK_UNLOCK:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            break;

        case TK_UPDATE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_UPDATE;
            info->is_real_query = true;
            break;

        case TK_TRUNCATE:
            info->status = QC_QUERY_TOKENIZED;
            info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->is_real_query = true;
            break;

        default:
            ;
        }
    }
    else if (!info->keyword_2)
    {
        info->keyword_2 = token;

        switch (info->keyword_1)
        {
        case TK_CHECK:
            if (info->keyword_2 == TK_TABLE)
            {
                info->status = QC_QUERY_TOKENIZED;
                info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            }
            break;

        case TK_DEALLOCATE:
            if (info->keyword_2 == TK_PREPARE)
            {
                info->status = QC_QUERY_TOKENIZED;
                info->types = QUERY_TYPE_SESSION_WRITE;
            }
            break;

        case TK_LOAD:
            if (info->keyword_2 == TK_DATA)
            {
                info->status = QC_QUERY_TOKENIZED;
                info->types = QUERY_TYPE_WRITE;
                info->operation = QUERY_OP_LOAD;
            }
            break;

        case TK_RENAME:
            if (info->keyword_2 == TK_TABLE)
            {
                info->status = QC_QUERY_TOKENIZED;
                info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            }
            break;

        case TK_START:
            switch (info->keyword_2)
            {
            case TK_TRANSACTION:
                info->status = QC_QUERY_TOKENIZED;
                info->types = QUERY_TYPE_BEGIN_TRX;
                break;

            default:
                break;
            }
            break;

        case TK_SHOW:
            switch (info->keyword_2)
            {
            case TK_DATABASES_KW:
                info->status = QC_QUERY_TOKENIZED;
                info->types = QUERY_TYPE_SHOW_DATABASES;
                break;

            case TK_TABLES:
                info->status = QC_QUERY_TOKENIZED;
                info->types = QUERY_TYPE_SHOW_TABLES;
                break;

            default:
                break;
            }
        }
    }
}

void maxscaleRenameTable(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT;

    for (int i = 0; i < pTables->nSrc; ++i)
    {
        const struct SrcList_item* pItem = &pTables->a[i];

        ss_dassert(pItem->zName);
        ss_dassert(pItem->zAlias);

        update_names(info, pItem->zDatabase, pItem->zName);
        update_names(info, NULL, pItem->zAlias); // The new name is passed in the alias field.
    }

    exposed_sqlite3SrcListDelete(pParse->db, pTables);
}

void maxscalePrepare(Parse* pParse, Token* pName, Token* pStmt)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_PREPARE_NAMED_STMT;
    info->is_real_query = true;
}

void maxscalePrivileges(Parse* pParse, int kind)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

    switch (kind)
    {
    case TK_GRANT:
        info->operation = QUERY_OP_GRANT;
        break;

    case TK_REVOKE:
        info->operation = QUERY_OP_REVOKE;
        break;

    default:
        ss_dassert(!true);
    }
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

void maxscaleSet(Parse* pParse, int scope, mxs_set_t kind, ExprList* pList)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = 0; // Reset what was set in maxscaleKeyword

    switch (kind)
    {
    case MXS_SET_TRANSACTION:
        if ((scope == TK_GLOBAL) || (scope == TK_SESSION))
        {
            info->types = QUERY_TYPE_GSYSVAR_WRITE;
        }
        else
        {
            ss_dassert(scope == 0);
            info->types = QUERY_TYPE_WRITE;
        }
        break;

    case MXS_SET_VARIABLES:
        {
            for (int i = 0; i < pList->nExpr; ++i)
            {
                const struct ExprList_item* pItem = &pList->a[i];

                switch (pItem->pExpr->op)
                {
                case TK_CHARACTER:
                case TK_NAMES:
                    info->types |= QUERY_TYPE_GSYSVAR_WRITE;
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
                            info->types |= QUERY_TYPE_USERVAR_WRITE;
                        }
                        else
                        {
                            info->types |= QUERY_TYPE_GSYSVAR_WRITE;
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
                                    info->types |= QUERY_TYPE_BEGIN_TRX;
                                    info->types |= QUERY_TYPE_DISABLE_AUTOCOMMIT;
                                    break;

                                case 1:
                                    info->types |= QUERY_TYPE_ENABLE_AUTOCOMMIT;
                                    info->types |= QUERY_TYPE_COMMIT;
                                    break;

                                default:
                                    break;
                                }
                            }
                        }

                        if (pValue->op == TK_SELECT)
                        {
                            update_affected_fields_from_select(info, pValue->x.pSelect, NULL);
                            info->is_real_query = false; // TODO: This is what qc_mysqlembedded claims.
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

extern void maxscaleShow(Parse* pParse, MxsShow* pShow)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;

    char* zDatabase = NULL;
    char* zName = NULL;

    char database[pShow->pDatabase ? pShow->pDatabase->n + 1 : 0];
    if (pShow->pDatabase)
    {
        strncpy(database, pShow->pDatabase->z, pShow->pDatabase->n);
        database[pShow->pDatabase->n] = 0;
        zDatabase = database;
    }

    char name[pShow->pName ? pShow->pName->n + 1 : 0];
    if (pShow->pName)
    {
        strncpy(name, pShow->pName->z, pShow->pName->n);
        name[pShow->pName->n] = 0;
        zName = name;
    }

    switch (pShow->what)
    {
    case MXS_SHOW_COLUMNS:
        {
            info->types = QUERY_TYPE_READ;
            update_names(info, zDatabase, zName);
            if (pShow->data == MXS_SHOW_COLUMNS_FULL)
            {
                append_affected_field(info,
                                      "COLLATION_NAME COLUMN_COMMENT COLUMN_DEFAULT "
                                      "COLUMN_KEY COLUMN_NAME COLUMN_TYPE EXTRA "
                                      "IS_NULLABLE PRIVILEGES");
            }
            else
            {
                append_affected_field(info,
                                      "COLUMN_DEFAULT COLUMN_KEY COLUMN_NAME "
                                      "COLUMN_TYPE EXTRA IS_NULLABLE");
            }
        }
        break;

    case MXS_SHOW_CREATE_VIEW:
        {
            info->types = QUERY_TYPE_WRITE;
            update_names(info, zDatabase, zName);
        }
        break;

    case MXS_SHOW_CREATE_TABLE:
        {
            info->types = QUERY_TYPE_WRITE;
            update_names(info, zDatabase, zName);
        }
        break;

    case MXS_SHOW_DATABASES:
        {
            info->types = QUERY_TYPE_SHOW_DATABASES;
            update_names(info, "information_schema", "SCHEMATA");
            append_affected_field(info, "SCHEMA_NAME");
        }
        break;

    case MXS_SHOW_INDEX:
    case MXS_SHOW_INDEXES:
    case MXS_SHOW_KEYS:
        {
            info->types = QUERY_TYPE_WRITE;
            update_names(info, "information_schema", "STATISTICS");
            append_affected_field(info,
                                  "CARDINALITY COLLATION COLUMN_NAME COMMENT INDEX_COMMENT "
                                  "INDEX_NAME INDEX_TYPE NON_UNIQUE NULLABLE PACKED SEQ_IN_INDEX "
                                  "SUB_PART TABLE_NAME");
        }
        break;

    case MXS_SHOW_TABLE_STATUS:
        {
            info->types = QUERY_TYPE_WRITE;
            update_names(info, "information_schema", "TABLES");
            append_affected_field(info,
                                  "AUTO_INCREMENT AVG_ROW_LENGTH CHECKSUM CHECK_TIME "
                                  "CREATE_OPTIONS CREATE_TIME DATA_FREE DATA_LENGTH "
                                  "ENGINE INDEX_LENGTH MAX_DATA_LENGTH ROW_FORMAT "
                                  "TABLE_COLLATION TABLE_COMMENT TABLE_NAME "
                                  "TABLE_ROWS UPDATE_TIME VERSION");
        }
        break;

    case MXS_SHOW_STATUS:
        {
            switch (pShow->data)
            {
            case MXS_SHOW_VARIABLES_GLOBAL:
            case MXS_SHOW_VARIABLES_SESSION:
            case MXS_SHOW_VARIABLES_UNSPECIFIED:
                // TODO: qc_mysqlembedded does not set the type bit.
                info->types = QUERY_TYPE_UNKNOWN;
                update_names(info, "information_schema", "SESSION_STATUS");
                append_affected_field(info, "VARIABLE_NAME VARIABLE_VALUE");
                break;

            case MXS_SHOW_STATUS_MASTER:
                info->types = QUERY_TYPE_WRITE;
                break;

            case MXS_SHOW_STATUS_SLAVE:
                info->types = QUERY_TYPE_READ;
                break;

            case MXS_SHOW_STATUS_ALL_SLAVES:
                info->types = QUERY_TYPE_READ;
                break;

            default:
                break;
            }
        }
        break;

    case MXS_SHOW_TABLES:
        {
            info->types = QUERY_TYPE_SHOW_TABLES;
            update_names(info, "information_schema", "TABLE_NAMES");
            append_affected_field(info, "TABLE_NAME");
        }
        break;

    case MXS_SHOW_VARIABLES:
        {
            if (pShow->data == MXS_SHOW_VARIABLES_GLOBAL)
            {
                info->types = QUERY_TYPE_GSYSVAR_READ;
            }
            else
            {
                info->types = QUERY_TYPE_SYSVAR_READ;
            }
            update_names(info, "information_schema", "SESSION_VARIABLES");
            append_affected_field(info, "VARIABLE_NAME VARIABLE_VALUE");
        }
        break;

    case MXS_SHOW_WARNINGS:
        {
            // qc_mysqliembedded claims this.
            info->types = QUERY_TYPE_WRITE;
        }
        break;

    default:
        ss_dassert(!true);
    }
}

void maxscaleTruncate(Parse* pParse, Token* pDatabase, Token* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_TRUNCATE;
    info->is_real_query = true;

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

    update_names(info, zDatabase, name);
}

void maxscaleUse(Parse* pParse, Token* pToken)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->types = QUERY_TYPE_SESSION_WRITE;
    info->operation = QUERY_OP_CHANGE_DB;
}

/**
 * API
 */
static bool qc_sqlite_init(const char* args);
static void qc_sqlite_end(void);
static bool qc_sqlite_thread_init(void);
static void qc_sqlite_thread_end(void);
static qc_parse_result_t qc_sqlite_parse(GWBUF* query);
static uint32_t qc_sqlite_get_type(GWBUF* query);
static qc_query_op_t qc_sqlite_get_operation(GWBUF* query);
static char* qc_sqlite_get_created_table_name(GWBUF* query);
static bool qc_sqlite_is_drop_table_query(GWBUF* query);
static bool qc_sqlite_is_real_query(GWBUF* query);
static char** qc_sqlite_get_table_names(GWBUF* query, int* tblsize, bool fullnames);
static char* qc_sqlite_get_canonical(GWBUF* query);
static bool qc_sqlite_query_has_clause(GWBUF* query);
static char* qc_sqlite_get_affected_fields(GWBUF* query);
static char** qc_sqlite_get_database_names(GWBUF* query, int* sizep);

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

static char ARG_LOG_UNRECOGNIZED_STATEMENTS[] = "log_unrecognized_statements";

static bool qc_sqlite_init(const char* args)
{
    QC_TRACE();
    assert(!this_unit.initialized);

    qc_log_level_t log_level = QC_LOG_NOTHING;

    if (args)
    {
        char arg[strlen(args) + 1];
        strcpy(arg, args);

        const char* key;
        const char* value;

        if (get_key_and_value(arg, &key, &value))
        {
            if (strcmp(key, ARG_LOG_UNRECOGNIZED_STATEMENTS) == 0)
            {
                char *end;

                long l = strtol(value, &end, 0);

                if ((*end == 0) && (l >= QC_LOG_NOTHING) && (l <= QC_LOG_NON_TOKENIZED))
                {
                    log_level = l;
                }
                else
                {
                    MXS_WARNING("qc_sqlite: '%s' is not a number between %d and %d.",
                                value, QC_LOG_NOTHING, QC_LOG_NON_TOKENIZED);
                }
            }
            else
            {
                MXS_WARNING("qc_sqlite: '%s' is not a recognized argument.", key);
            }
        }
        else
        {
            MXS_WARNING("qc_sqlite: '%s' is not a recognized argument string.", args);
        }
    }

    if (sqlite3_initialize() == 0)
    {
        init_builtin_functions();

        this_unit.initialized = true;
        this_unit.log_level = log_level;

        if (qc_sqlite_thread_init())
        {
            if (log_level != QC_LOG_NOTHING)
            {
                const char* message;

                switch (log_level)
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

                MXS_NOTICE("qc_sqlite: %s", message);
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

    return this_unit.initialized;
}

static void qc_sqlite_end(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);

    finish_builtin_functions();

    qc_sqlite_thread_end();

    sqlite3_shutdown();
    this_unit.initialized = false;
}

static bool qc_sqlite_thread_init(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(!this_thread.initialized);

    // TODO: It may be sufficient to have a single in-memory database for all threads.
    int rc = sqlite3_open(":memory:", &this_thread.db);
    if (rc == SQLITE_OK)
    {
        MXS_INFO("qc_sqlite: In-memory sqlite database successfully opened for thread %lu.",
                 (unsigned long) pthread_self());

        QC_SQLITE_INFO* info = info_alloc();

        if (info)
        {
            this_thread.info = info;

            // With this statement we cause sqlite3 to initialize itself, so that it
            // is not done as part of the actual classification of data.
            const char* s = "CREATE TABLE __maxscale__internal__ (int field UNIQUE)";
            size_t len = strlen(s);

            this_thread.info->query = s;
            this_thread.info->query_len = len;
            this_thread.info->initializing = true;
            parse_query_string(s, len);
            this_thread.info->initializing = false;
            this_thread.info->query = NULL;
            this_thread.info->query_len = 0;

            info_free(this_thread.info);
            this_thread.info = NULL;

            this_thread.initialized = true;
        }
        else
        {
            sqlite3_close(this_thread.db);
            this_thread.db = NULL;
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: Failed to open in-memory sqlite database for thread %lu: %d, %s",
                  (unsigned long) pthread_self(), rc, sqlite3_errstr(rc));
    }

    return this_thread.initialized;
}

static void qc_sqlite_thread_end(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    ss_dassert(this_thread.db);
    int rc = sqlite3_close(this_thread.db);

    if (rc != SQLITE_OK)
    {
        MXS_WARNING("qc_sqlite: The closing of the thread specific sqlite database failed: %d, %s",
                    rc, sqlite3_errstr(rc));
    }

    this_thread.db = NULL;
    this_thread.initialized = false;
}

static qc_parse_result_t qc_sqlite_parse(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    QC_SQLITE_INFO* info = get_query_info(query);

    return info ? info->status : QC_QUERY_INVALID;
}

static uint32_t qc_sqlite_get_type(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    uint32_t types = QUERY_TYPE_UNKNOWN;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            types = info->types;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report query type");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return types;
}

static qc_query_op_t qc_sqlite_get_operation(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    qc_query_op_t op = QUERY_OP_UNDEFINED;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            op = info->operation;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report query operation");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return op;
}

static char* qc_sqlite_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    char* created_table_name = NULL;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            if (info->created_table_name)
            {
                created_table_name = mxs_strdup(info->created_table_name);
            }
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report created tables");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return created_table_name;
}

static bool qc_sqlite_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    bool is_drop_table = false;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            is_drop_table = info->is_drop_table;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report whether query is drop table");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return is_drop_table;
}

static bool qc_sqlite_is_real_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    bool is_real_query = false;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            is_real_query = info->is_real_query;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report whether query is a real query");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return is_real_query;
}

static char** qc_sqlite_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    char** table_names = NULL;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            if (fullnames)
            {
                table_names = info->table_fullnames;
            }
            else
            {
                table_names = info->table_names;
            }

            if (table_names)
            {
                table_names = copy_string_array(table_names, tblsize);
            }
            else
            {
                *tblsize = 0;
            }
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report what tables are accessed");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return table_names;
}

static char* qc_sqlite_get_canonical(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_canonical not implemented yet.");

    return NULL;
}

static bool qc_sqlite_query_has_clause(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    bool has_clause = false;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            has_clause = info->has_clause;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report whether the query has a where clause");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return has_clause;
}

static char* qc_sqlite_get_affected_fields(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    char* affected_fields = NULL;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            affected_fields = info->affected_fields;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report what fields are affected");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    if (!affected_fields)
    {
        affected_fields = "";
    }

    return mxs_strdup(affected_fields);
}

static char** qc_sqlite_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    char** database_names = NULL;
    QC_SQLITE_INFO* info = get_query_info(query);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            if (info->database_names)
            {
                database_names = copy_string_array(info->database_names, sizep);
            }
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report what databases are accessed");
        }
    }
    else
    {
        MXS_ERROR("qc_sqlite: The query could not be parsed. Response not valid.");
    }

    return database_names;
}

/**
 * EXPORTS
 */

static char version_string[] = "V1.0.0";

static QUERY_CLASSIFIER qc =
{
    qc_sqlite_init,
    qc_sqlite_end,
    qc_sqlite_thread_init,
    qc_sqlite_thread_end,
    qc_sqlite_parse,
    qc_sqlite_get_type,
    qc_sqlite_get_operation,
    qc_sqlite_get_created_table_name,
    qc_sqlite_is_drop_table_query,
    qc_sqlite_is_real_query,
    qc_sqlite_get_table_names,
    NULL,
    qc_sqlite_query_has_clause,
    qc_sqlite_get_affected_fields,
    qc_sqlite_get_database_names,
};


MODULE_INFO info =
{
    MODULE_API_QUERY_CLASSIFIER,
    MODULE_GA,
    QUERY_CLASSIFIER_VERSION,
    "Query classifier using sqlite.",
};

char* version()
{
    return version_string;
}

void ModuleInit()
{
}

QUERY_CLASSIFIER* GetModuleObject()
{
    return &qc;
}
