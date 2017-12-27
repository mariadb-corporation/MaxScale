/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "qc_sqlite"
#include <sqliteInt.h>

#include <signal.h>
#include <string.h>
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
    uint32_t collect;                // What information should be collected.
    uint32_t collected;              // What information has been collected.
    const char* query;               // The query passed to sqlite.
    size_t query_len;                // The length of the query.

    uint32_t type_mask;              // The type mask of the query.
    qc_query_op_t operation;         // The operation in question.
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
    char* prepare_name;              // The name of a prepared statement.
    GWBUF* preparable_stmt;          // The preparable statement.
    QC_FIELD_INFO *field_infos;      // Pointer to array of QC_FIELD_INFOs.
    size_t field_infos_len;          // The used entries in field_infos.
    size_t field_infos_capacity;     // The capacity of the field_infos array.
    QC_FUNCTION_INFO *function_infos;// Pointer to array of QC_FUNCTION_INFOs.
    size_t function_infos_len;       // The used entries in function_infos.
    size_t function_infos_capacity;  // The capacity of the function_infos array.
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
    bool setup;
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
 * HELPERS
 */

typedef enum qc_token_position
{
    QC_TOKEN_MIDDLE, // In the middle or irrelevant, e.g.: "=" in "a = b".
    QC_TOKEN_LEFT,   // To the left, e.g.: "a" in "a = b".
    QC_TOKEN_RIGHT,  // To the right, e.g: "b" in "a = b".
} qc_token_position_t;

static void buffer_object_free(void* data);
static char** copy_string_array(char** strings, int* pn);
static void enlarge_string_array(size_t n, size_t len, char*** ppzStrings, size_t* pCapacity);
static bool ensure_query_is_parsed(GWBUF* query, uint32_t collect);
static void free_field_infos(QC_FIELD_INFO* infos, size_t n_infos);
static void free_string_array(char** sa);
static QC_SQLITE_INFO* get_query_info(GWBUF* query, uint32_t collect);
static QC_SQLITE_INFO* info_alloc(uint32_t collect);
static void info_finish(QC_SQLITE_INFO* info);
static void info_free(QC_SQLITE_INFO* info);
static QC_SQLITE_INFO* info_init(QC_SQLITE_INFO* info, uint32_t collect);
static void log_invalid_data(GWBUF* query, const char* message);
static bool parse_query(GWBUF* query, uint32_t collect);
static void parse_query_string(const char* query, size_t len);
static bool query_is_parsed(GWBUF* query, uint32_t collect);
static bool should_exclude(const char* zName, const ExprList* pExclude);
static void update_field_info(QC_SQLITE_INFO* info,
                              const char* database,
                              const char* table,
                              const char* column,
                              uint32_t usage,
                              const ExprList* pExclude);
static void update_field_infos_from_expr(QC_SQLITE_INFO* info,
                                         const struct Expr* pExpr,
                                         uint32_t usage,
                                         const ExprList* pExclude);
static void update_field_infos(QC_SQLITE_INFO* info,
                               int prev_token,
                               const Expr* pExpr,
                               uint32_t usage,
                               qc_token_position_t pos,
                               const ExprList* pExclude);
static void update_field_infos_from_exprlist(QC_SQLITE_INFO* info,
                                             const ExprList* pEList,
                                             uint32_t usage,
                                             const ExprList* pExclude);
static void update_field_infos_from_idlist(QC_SQLITE_INFO* info,
                                           const IdList* pIds,
                                           uint32_t usage,
                                           const ExprList* pExclude);
typedef enum compound_approach
{
    ANALYZE_COMPOUND_SELECTS,
    IGNORE_COMPOUND_SELECTS
} compound_approach_t;
static void update_field_infos_from_select_compound(QC_SQLITE_INFO* info,
                                                    const Select* pSelect,
                                                    uint32_t usage,
                                                    const ExprList* pExclude,
                                                    compound_approach_t compound_approach);
#define update_field_infos_from_select(i, s, u, e)\
    update_field_infos_from_select_compound(i, s, u, e, ANALYZE_COMPOUND_SELECTS)
static void update_function_info(QC_SQLITE_INFO* info,
                                 const char* name,
                                 uint32_t usage);
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
extern void maxscaleCollectInfoFromSelect(Parse*, Select*, int);

extern void maxscale_update_function_info(const char* name, uint32_t usage);

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

    ss = (char**) MXS_MALLOC((*pn + 1) * sizeof(char*));
    MXS_ABORT_IF_NULL(ss);

    ss[*pn] = 0;

    for (int i = 0; i < *pn; ++i)
    {
        ss[i] = MXS_STRDUP(strings[i]);
        MXS_ABORT_IF_NULL(ss[i]);
    }

    return ss;
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

static void free_field_infos(QC_FIELD_INFO* infos, size_t n_infos)
{
    if (infos)
    {
        for (int i = 0; i < n_infos; ++i)
        {
            MXS_FREE(infos[i].database);
            MXS_FREE(infos[i].table);
            MXS_FREE(infos[i].column);
        }

        MXS_FREE(infos);
    }
}

static void free_function_infos(QC_FUNCTION_INFO* infos, size_t n_infos)
{
    if (infos)
    {
        for (int i = 0; i < n_infos; ++i)
        {
            MXS_FREE(infos[i].name);
        }

        MXS_FREE(infos);
    }
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

static QC_SQLITE_INFO* get_query_info(GWBUF* query, uint32_t collect)
{
    QC_SQLITE_INFO* info = NULL;

    if (ensure_query_is_parsed(query, collect))
    {
        info = (QC_SQLITE_INFO*) gwbuf_get_buffer_object_data(query, GWBUF_PARSING_INFO);
        ss_dassert(info);
    }

    return info;
}

static QC_SQLITE_INFO* info_alloc(uint32_t collect)
{
    QC_SQLITE_INFO* info = MXS_MALLOC(sizeof(*info));
    MXS_ABORT_IF_NULL(info);

    info_init(info, collect);

    return info;
}

static void info_finish(QC_SQLITE_INFO* info)
{
    free_string_array(info->table_names);
    free_string_array(info->table_fullnames);
    free(info->created_table_name);
    free_string_array(info->database_names);
    free(info->prepare_name);
    gwbuf_free(info->preparable_stmt);
    free_field_infos(info->field_infos, info->field_infos_len);
    free_function_infos(info->function_infos, info->function_infos_len);
}

static void info_free(QC_SQLITE_INFO* info)
{
    if (info)
    {
        info_finish(info);
        free(info);
    }
}

static QC_SQLITE_INFO* info_init(QC_SQLITE_INFO* info, uint32_t collect)
{
    memset(info, 0, sizeof(*info));

    info->status = QC_QUERY_INVALID;
    info->collect = collect;
    info->collected = 0;

    info->type_mask = QUERY_TYPE_UNKNOWN;
    info->operation = QUERY_OP_UNDEFINED;
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
    info->prepare_name = NULL;
    info->preparable_stmt = NULL;
    info->field_infos = NULL;
    info->field_infos_len = 0;
    info->field_infos_capacity = 0;
    info->function_infos = NULL;
    info->function_infos_len = 0;
    info->function_infos_capacity = 0;
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
                "Statement was classified only based on keywords "
                "(Sqlite3 error: %s, %s): \"%.*s%s\"";
        }
        else
        {
            if (qc_info_was_parsed(this_thread.info->status))
            {
                format =
                    "Statement was only partially parsed "
                    "(Sqlite3 error: %s, %s): \"%.*s%s\"";

                // The status was set to QC_QUERY_PARSED, but sqlite3 returned an
                // error. Most likely, query contains some excess unrecognized stuff.
                this_thread.info->status = QC_QUERY_PARTIALLY_PARSED;
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
    else if (!this_thread.info->initializing) // If we are initializing, the query will not be classified.
    {
        if (this_unit.log_level > QC_LOG_NOTHING)
        {
            if (qc_info_was_tokenized(this_thread.info->status))
            {
                // This suggests a callback from the parser into this module is not made.
                format =
                    "Statement was classified only based on keywords, "
                    "even though the statement was parsed: \"%.*s%s\"";

                MXS_WARNING(format, l, query, suffix);
            }
            else if (!qc_info_was_parsed(this_thread.info->status))
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
                QC_SQLITE_INFO* info =
                    (QC_SQLITE_INFO*) gwbuf_get_buffer_object_data(query, GWBUF_PARSING_INFO);

                if (info)
                {
                    ss_dassert((~info->collect & collect) != 0);
                    ss_dassert((~info->collected & collect) != 0);

                    // If we get here, then the statement has been parsed once, but
                    // not all needed was collected. Now we turn on all blinkelichts to
                    // ensure that a statement is parsed at most twice.
                    info->collect = QC_COLLECT_ALL;
                }
                else
                {
                    info = info_alloc(collect);

                    if (info)
                    {
                        // TODO: Add return value to gwbuf_add_buffer_object.
                        gwbuf_add_buffer_object(query, GWBUF_PARSING_INFO, info, buffer_object_free);
                    }
                }

                if (info)
                {
                    this_thread.info = info;

                    size_t len = MYSQL_GET_PAYLOAD_LEN(data) - 1; // Subtract 1 for packet type byte.

                    const char* s = (const char*) &data[MYSQL_HEADER_LEN + 1];

                    this_thread.info->query = s;
                    this_thread.info->query_len = len;
                    parse_query_string(s, len);
                    this_thread.info->query = NULL;
                    this_thread.info->query_len = 0;

                    if (command == MYSQL_COM_STMT_PREPARE)
                    {
                        info->type_mask |= QUERY_TYPE_PREPARE_STMT;
                    }

                    info->collected = info->collect;

                    parsed = true;

                    this_thread.info = NULL;
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
        QC_SQLITE_INFO* info = (QC_SQLITE_INFO*) gwbuf_get_buffer_object_data(query, GWBUF_PARSING_INFO);
        ss_dassert(info);

        if ((~info->collected & collect) != 0)
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
            if (length > GWBUF_LENGTH(query) - MYSQL_HEADER_LEN - 1)
            {
                length = GWBUF_LENGTH(query) - MYSQL_HEADER_LEN - 1;
            }

            MXS_INFO("Parsing the query failed, %s: %*s", message, length, sql);
        }
    }
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

static void update_field_info(QC_SQLITE_INFO* info,
                              const char* database,
                              const char* table,
                              const char* column,
                              uint32_t usage,
                              const ExprList* pExclude)
{
    ss_dassert(column);

    if (!(info->collect & QC_COLLECT_FIELDS) || (info->collected & QC_COLLECT_FIELDS))
    {
        // If field information should not be collected, or if field information
        // has already been collected, we just return.
        return;
    }

    QC_FIELD_INFO item = { (char*)database, (char*)table, (char*)column, usage };

    int i;
    for (i = 0; i < info->field_infos_len; ++i)
    {
        QC_FIELD_INFO* field_info = info->field_infos + i;

        if (strcasecmp(item.column, field_info->column) == 0)
        {
            if (!item.table && !field_info->table)
            {
                ss_dassert(!item.database && !field_info->database);
                break;
            }
            else if (item.table && field_info->table && (strcmp(item.table, field_info->table) == 0))
            {
                if (!item.database && !field_info->database)
                {
                    break;
                }
                else if (item.database &&
                         field_info->database &&
                         (strcmp(item.database, field_info->database) == 0))
                {
                    break;
                }
            }
        }
    }

    QC_FIELD_INFO* field_infos = NULL;

    if (i == info->field_infos_len) // If true, the field was not present already.
    {
        // If only a column is specified, but not a table or database and we
        // have a list of expressions that should be excluded, we check if the column
        // value is present in that list. This is in order to exclude the second "d" in
        // a statement like "select a as d from x where d = 2".
        if (!(column && !table && !database && pExclude && should_exclude(column, pExclude)))
        {
            if (info->field_infos_len < info->field_infos_capacity)
            {
                field_infos = info->field_infos;
            }
            else
            {
                size_t capacity = info->field_infos_capacity ? 2 * info->field_infos_capacity : 8;
                field_infos = MXS_REALLOC(info->field_infos, capacity * sizeof(QC_FIELD_INFO));

                if (field_infos)
                {
                    info->field_infos = field_infos;
                    info->field_infos_capacity = capacity;
                }
            }
        }
    }
    else
    {
        info->field_infos[i].usage |= usage;
    }

    // If field_infos is NULL, then the field was found and has already been noted.
    if (field_infos)
    {
        item.database = item.database ? MXS_STRDUP(item.database) : NULL;
        item.table = item.table ? MXS_STRDUP(item.table) : NULL;
        ss_dassert(item.column);
        item.column = MXS_STRDUP(item.column);

        // We are happy if we at least could dup the column.

        if (item.column)
        {
            field_infos[info->field_infos_len++] = item;
        }
    }
}

static void update_function_info(QC_SQLITE_INFO* info,
                                 const char* name,
                                 uint32_t usage)
{
    ss_dassert(name);

    if (!(info->collect & QC_COLLECT_FUNCTIONS) || (info->collected & QC_COLLECT_FUNCTIONS))
    {
        // If function information should not be collected, or if function information
        // has already been collected, we just return.
        return;
    }

    QC_FUNCTION_INFO item = { (char*)name, usage };

    int i;
    for (i = 0; i < info->function_infos_len; ++i)
    {
        QC_FUNCTION_INFO* function_info = info->function_infos + i;

        if (strcasecmp(item.name, function_info->name) == 0)
        {
            break;
        }
    }

    QC_FUNCTION_INFO* function_infos = NULL;

    if (i == info->function_infos_len) // If true, the function was not present already.
    {
        if (info->function_infos_len < info->function_infos_capacity)
        {
            function_infos = info->function_infos;
        }
        else
        {
            size_t capacity = info->function_infos_capacity ? 2 * info->function_infos_capacity : 8;
            function_infos = MXS_REALLOC(info->function_infos, capacity * sizeof(QC_FUNCTION_INFO));

            if (function_infos)
            {
                info->function_infos = function_infos;
                info->function_infos_capacity = capacity;
            }
        }
    }
    else
    {
        info->function_infos[i].usage |= usage;
    }

    // If function_infos is NULL, then the function was found and has already been noted.
    if (function_infos)
    {
        ss_dassert(item.name);
        item.name = MXS_STRDUP(item.name);

        if (item.name)
        {
            function_infos[info->function_infos_len++] = item;
        }
    }
}

extern void maxscale_update_function_info(const char* name, uint32_t usage)
{
    QC_SQLITE_INFO* info = this_thread.info;

    update_function_info(info, name, usage);
}

static void update_field_infos_from_expr(QC_SQLITE_INFO* info,
                                         const struct Expr* pExpr,
                                         uint32_t usage,
                                         const ExprList* pExclude)
{
    QC_FIELD_INFO item = {};

    if (pExpr->op == TK_ASTERISK)
    {
        item.column = "*";
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
                item.column = "*";
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
                item.column = "*";
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
            update_field_info(info, item.database, item.table, item.column, usage, pExclude);
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

static void update_field_infos(QC_SQLITE_INFO* info,
                               int prev_token,
                               const Expr* pExpr,
                               uint32_t usage,
                               qc_token_position_t pos,
                               const ExprList* pExclude)
{
    const char* zToken = pExpr->u.zToken;

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
                    // TODO: This should actually be "... && (info->operation == QUERY_OP_SET)"
                    // TODO: but there is no QUERY_OP_SET at the moment.
                    if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT) &&
                        (info->operation != QUERY_OP_SELECT))
                    {
                        info->type_mask |= QUERY_TYPE_GSYSVAR_WRITE;
                    }
                    else
                    {
                        if ((strcasecmp(&zToken[2], "identity") == 0) ||
                            (strcasecmp(&zToken[2], "last_insert_id") == 0))
                        {
                            info->type_mask |= QUERY_TYPE_MASTER_READ;
                        }
                        else
                        {
                            info->type_mask |= QUERY_TYPE_SYSVAR_READ;
                        }
                    }
                }
                else
                {
                    if ((prev_token == TK_EQ) && (pos == QC_TOKEN_LEFT))
                    {
                        info->type_mask |= QUERY_TYPE_USERVAR_WRITE;
                    }
                    else
                    {
                        info->type_mask |= QUERY_TYPE_USERVAR_READ;
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
        case TK_REM:
        case TK_SLASH:
        case TK_STAR:
        case TK_UMINUS:
            update_function_info(info, get_token_symbol(pExpr->op), usage);
            break;

        case TK_FUNCTION:
            if (zToken)
            {
                if (strcasecmp(zToken, "last_insert_id") == 0)
                {
                    info->type_mask |= (QUERY_TYPE_READ | QUERY_TYPE_MASTER_READ);
                }
                else if (!is_builtin_readonly_function(zToken))
                {
                    info->type_mask |= QUERY_TYPE_WRITE;
                }

                // We exclude "row", because we cannot detect all rows the same
                // way qc_mysqlembedded does.
                if (strcasecmp(zToken, "row") != 0)
                {
                    update_function_info(info, zToken, usage);
                }
            }
            break;

        default:
            break;
        }

        if (pExpr->pLeft)
        {
            update_field_infos(info, pExpr->op, pExpr->pLeft, usage, QC_TOKEN_LEFT, pExclude);
        }

        if (pExpr->pRight)
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
                update_field_infos_from_exprlist(info, pExpr->x.pList, usage, pExclude);
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

static void update_field_infos_from_exprlist(QC_SQLITE_INFO* info,
                                             const ExprList* pEList,
                                             uint32_t usage,
                                             const ExprList* pExclude)
{
    for (int i = 0; i < pEList->nExpr; ++i)
    {
        struct ExprList_item* pItem = &pEList->a[i];

        update_field_infos(info, 0, pItem->pExpr, usage, QC_TOKEN_MIDDLE, pExclude);
    }
}

static void update_field_infos_from_idlist(QC_SQLITE_INFO* info,
                                           const IdList* pIds,
                                           uint32_t usage,
                                           const ExprList* pExclude)
{
    for (int i = 0; i < pIds->nId; ++i)
    {
        struct IdList_item* pItem = &pIds->a[i];

        update_field_info(info, NULL, NULL, pItem->zName, usage, pExclude);
    }
}

static void update_field_infos_from_select_compound(QC_SQLITE_INFO* info,
                                                    const Select* pSelect,
                                                    uint32_t usage,
                                                    const ExprList* pExclude,
                                                    compound_approach_t compound_approach)
{
    if (pSelect->pSrc)
    {
        const SrcList* pSrc = pSelect->pSrc;

        for (int i = 0; i < pSrc->nSrc; ++i)
        {
            if (pSrc->a[i].zName)
            {
                update_names(info, pSrc->a[i].zDatabase, pSrc->a[i].zName);
            }

            if (pSrc->a[i].pSelect)
            {
                uint32_t sub_usage = usage;

                sub_usage &= ~QC_USED_IN_SELECT;
                sub_usage |= QC_USED_IN_SUBSELECT;

                update_field_infos_from_select(info, pSrc->a[i].pSelect, sub_usage, pExclude);
            }

#ifdef QC_COLLECT_NAMES_FROM_USING
            // With this enabled, the affected fields of
            //    select * from (t1 as t2 left join t1 as t3 using (a)), t1;
            // will be "* a", otherwise "*". However, that "a" is used in the join
            // does not reveal its value, right?
            if (pSrc->a[i].pUsing)
            {
                update_field_infos_from_idlist(info, pSrc->a[i].pUsing, 0, pSelect->pEList);
            }
#endif
        }
    }

    if (pSelect->pEList)
    {
        update_field_infos_from_exprlist(info, pSelect->pEList, usage, NULL);
    }

    if (pSelect->pWhere)
    {
        info->has_clause = true;
        update_field_infos(info, 0, pSelect->pWhere, QC_USED_IN_WHERE, QC_TOKEN_MIDDLE, pSelect->pEList);
    }

    if (pSelect->pGroupBy)
    {
        update_field_infos_from_exprlist(info, pSelect->pGroupBy, QC_USED_IN_GROUP_BY, pSelect->pEList);
    }

    if (pSelect->pHaving)
    {
        info->has_clause = true;
#if defined(COLLECT_HAVING_AS_WELL)
        // A HAVING clause can only refer to fields that already have been
        // mentioned. Consequently, they need not be collected.
        update_field_infos(info, 0, pSelect->pHaving, 0, QC_TOKEN_MIDDLE, pSelect->pEList);
#endif
    }

    if (compound_approach == ANALYZE_COMPOUND_SELECTS)
    {
        if (((pSelect->op == TK_UNION) || (pSelect->op == TK_ALL)) && pSelect->pPrior)
        {
            const Select* pPrior = pSelect->pPrior;

            while (pPrior)
            {
                update_field_infos_from_select_compound(info, pPrior, usage, pExclude,
                                                        IGNORE_COMPOUND_SELECTS);
                pPrior = pPrior->pPrior;
            }
        }
    }
}

static void update_database_names(QC_SQLITE_INFO* info, const char* zDatabase)
{
    char* zCopy = MXS_STRDUP(zDatabase);
    MXS_ABORT_IF_NULL(zCopy);
    exposed_sqlite3Dequote(zCopy);

    enlarge_string_array(1, info->database_names_len,
                         &info->database_names, &info->database_names_capacity);
    info->database_names[info->database_names_len++] = zCopy;
    info->database_names[info->database_names_len] = NULL;
}

static void update_names(QC_SQLITE_INFO* info, const char* zDatabase, const char* zTable)
{
    if ((info->collect & QC_COLLECT_TABLES) && !(info->collected & QC_COLLECT_TABLES))
    {
        char* zCopy = MXS_STRDUP(zTable);
        MXS_ABORT_IF_NULL(zCopy);
        // TODO: Is this call really needed. Check also sqlite3Dequote.
        exposed_sqlite3Dequote(zCopy);

        enlarge_string_array(1, info->table_names_len, &info->table_names, &info->table_names_capacity);
        info->table_names[info->table_names_len++] = zCopy;
        info->table_names[info->table_names_len] = NULL;

        if (zDatabase)
        {
            zCopy = MXS_MALLOC(strlen(zDatabase) + 1 + strlen(zTable) + 1);
            MXS_ABORT_IF_NULL(zCopy);

            strcpy(zCopy, zDatabase);
            strcat(zCopy, ".");
            strcat(zCopy, zTable);
            exposed_sqlite3Dequote(zCopy);
        }
        else
        {
            zCopy = MXS_STRDUP(zCopy);
            MXS_ABORT_IF_NULL(zCopy);
        }

        enlarge_string_array(1, info->table_fullnames_len,
                             &info->table_fullnames, &info->table_fullnames_capacity);
        info->table_fullnames[info->table_fullnames_len++] = zCopy;
        info->table_fullnames[info->table_fullnames_len] = NULL;
    }

    if ((info->collect & QC_COLLECT_DATABASES) && !(info->collected & QC_COLLECT_DATABASES))
    {
        if (zDatabase)
        {
            update_database_names(info, zDatabase);
        }
    }
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

    update_names_from_srclist(info, pSrcList);

    exposed_sqlite3SrcListDelete(pParse->db, pSrcList);
}

void mxs_sqlite3BeginTransaction(Parse* pParse, int type)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = QUERY_TYPE_BEGIN_TRX | type;
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

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
    info->type_mask = QUERY_TYPE_COMMIT;
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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
        update_field_infos_from_select(info, pSelect, QC_USED_IN_SELECT, NULL);
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
    info->type_mask = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_DELETE;
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
        update_field_infos(info, 0, pWhere, QC_USED_IN_WHERE, QC_TOKEN_MIDDLE, 0);
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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
    info->type_mask = QUERY_TYPE_WRITE;
    if (!isTemp)
    {
        info->type_mask |= QUERY_TYPE_COMMIT;
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
            update_field_infos_from_select(info, pSelect, QC_USED_IN_SELECT, NULL);
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
    info->type_mask = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_INSERT;
    ss_dassert(pTabList);
    ss_dassert(pTabList->nSrc >= 1);
    update_names_from_srclist(info, pTabList);

    if (pColumns)
    {
        update_field_infos_from_idlist(info, pColumns, 0, NULL);
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

        update_field_infos_from_select(info, pSelect, usage, NULL);
    }

    if (pSet)
    {
        update_field_infos_from_exprlist(info, pSet, 0, NULL);
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
    info->type_mask = QUERY_TYPE_ROLLBACK;
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

        maxscaleCollectInfoFromSelect(pParse, p, 0);
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
        info->type_mask = QUERY_TYPE_WRITE;

        if (isTemp)
        {
            info->type_mask |= QUERY_TYPE_CREATE_TMP_TABLE;
        }
        else
        {
            info->type_mask |= QUERY_TYPE_COMMIT;
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

        if (info->collect & QC_COLLECT_TABLES)
        {
            // If information is collected in several passes, then we may
            // this information already.
            if (!info->created_table_name)
            {
                info->created_table_name = MXS_STRDUP(info->table_names[0]);
                MXS_ABORT_IF_NULL(info->created_table_name);
            }
            else
            {
                ss_dassert(info->collect != info->collected);
                ss_dassert(strcmp(info->created_table_name, info->table_names[0]) == 0);
            }
        }
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
    info->type_mask = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_UPDATE;
    update_names_from_srclist(info, pTabList);
    info->has_clause = (pWhere ? true : false);

    if (pChanges)
    {
        for (int i = 0; i < pChanges->nExpr; ++i)
        {
            struct ExprList_item* pItem = &pChanges->a[i];

            update_field_infos(info, 0, pItem->pExpr, QC_USED_IN_SET, QC_TOKEN_MIDDLE, NULL);
        }
    }

    if (pWhere)
    {
        update_field_infos(info, 0, pWhere, QC_USED_IN_WHERE, QC_TOKEN_MIDDLE, pChanges);
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
    info->type_mask = QUERY_TYPE_WRITE;
}

void maxscaleCollectInfoFromSelect(Parse* pParse, Select* pSelect, int sub_select)
{
    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    if (pSelect->pInto)
    {
        // If there's a single variable, then it's a write.
        // mysql embedded considers it a system var write.
        info->type_mask = QUERY_TYPE_GSYSVAR_WRITE;

        // Also INTO {OUTFILE|DUMPFILE} will be typed as QUERY_TYPE_GSYSVAR_WRITE.
    }
    else
    {
        info->type_mask = QUERY_TYPE_READ;
    }

    uint32_t usage = sub_select ? QC_USED_IN_SUBSELECT : QC_USED_IN_SELECT;

    update_field_infos_from_select(info, pSelect, usage, NULL);
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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

void maxscaleCall(Parse* pParse, SrcList* pName, ExprList* pExprList)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = QUERY_TYPE_WRITE;
    info->operation = QUERY_OP_CALL;

    if (pExprList)
    {
        update_field_infos_from_exprlist(info, pExprList, 0, NULL);
    }

    exposed_sqlite3SrcListDelete(pParse->db, pName);
    exposed_sqlite3ExprListDelete(pParse->db, pExprList);
}

void maxscaleCheckTable(Parse* pParse, SrcList* pTables)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

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
        info->type_mask = QUERY_TYPE_READ;
    }
}

void maxscaleDeallocate(Parse* pParse, Token* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = QUERY_TYPE_WRITE;

    // If information is collected in several passes, then we may
    // this information already.
    if (!info->prepare_name)
    {
        info->prepare_name = MXS_MALLOC(pName->n + 1);
        if (info->prepare_name)
        {
            memcpy(info->prepare_name, pName->z, pName->n);
            info->prepare_name[pName->n] = 0;
        }
    }
    else
    {
        ss_dassert(info->collect != info->collected);
        ss_dassert(strncmp(info->prepare_name, pName->z, pName->n) == 0);
    }
}

void maxscaleDo(Parse* pParse, ExprList* pEList)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = (QUERY_TYPE_READ | QUERY_TYPE_WRITE);

    exposed_sqlite3ExprListDelete(pParse->db, pEList);
}

void maxscaleDrop(Parse* pParse, MxsDrop* pDrop)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_DROP;
}

void maxscaleExecute(Parse* pParse, Token* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = QUERY_TYPE_WRITE;

    // If information is collected in several passes, then we may
    // this information already.
    if (!info->prepare_name)
    {
        info->prepare_name = MXS_MALLOC(pName->n + 1);
        if (info->prepare_name)
        {
            memcpy(info->prepare_name, pName->z, pName->n);
            info->prepare_name[pName->n] = 0;
        }
    }
    else
    {
        ss_dassert(info->collect != info->collected);
        ss_dassert(strncmp(info->prepare_name, pName->z, pName->n) == 0);
    }
}

void maxscaleExplain(Parse* pParse, SrcList* pName)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = QUERY_TYPE_READ;
    update_names(info, pName->a[0].zDatabase, pName->a[0].zName);
    uint32_t u = QC_USED_IN_SELECT;
    update_field_info(info, "information_schema", "COLUMNS", "COLUMN_DEFAULT", u, NULL);
    update_field_info(info, "information_schema", "COLUMNS", "COLUMN_KEY", u, NULL);
    update_field_info(info, "information_schema", "COLUMNS", "COLUMN_NAME", u, NULL);
    update_field_info(info, "information_schema", "COLUMNS", "COLUMN_TYPE", u, NULL);
    update_field_info(info, "information_schema", "COLUMNS", "EXTRA", u, NULL);
    update_field_info(info, "information_schema", "COLUMNS", "IS_NULLABLE", u, NULL);

    exposed_sqlite3SrcListDelete(pParse->db, pName);
}

void maxscaleFlush(Parse* pParse, Token* pWhat)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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
            info->type_mask = QUERY_TYPE_WRITE;

            ss_dassert(pFullName->nSrc == 1);
            const struct SrcList_item* pItem = &pFullName->a[0];

            update_names(info, pItem->zDatabase, pItem->zName);
        }
        break;

    case MXS_HANDLER_CLOSE:
        {
            info->type_mask = QUERY_TYPE_WRITE;

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
    info->type_mask = QUERY_TYPE_WRITE;
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
    info->type_mask = QUERY_TYPE_WRITE;

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
            info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_ALTER;
            break;

        case TK_CALL:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            break;

        case TK_CREATE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_CREATE;
            break;

        case TK_DELETE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_DELETE;
            break;

        case TK_DESC:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_READ;
            break;

        case TK_DROP:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_DROP;
            break;

        case TK_EXECUTE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            break;

        case TK_EXPLAIN:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_READ;
            break;

        case TK_GRANT:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_GRANT;
            break;

        case TK_HANDLER:
            info->status = QUERY_TYPE_WRITE;
            break;

        case TK_INSERT:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_INSERT;
            break;

        case TK_LOCK:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            break;

        case TK_PREPARE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_PREPARE_NAMED_STMT;
            break;

        case TK_REPLACE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_INSERT;
            break;

        case TK_REVOKE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            info->operation = QUERY_OP_REVOKE;
            break;

        case TK_SELECT:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_READ;
            info->operation = QUERY_OP_SELECT;
            break;

        case TK_SET:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_GSYSVAR_WRITE;
            break;

        case TK_SHOW:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            break;

        case TK_START:
            // Will produce the right info for START SLAVE.
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            break;

        case TK_UNLOCK:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            break;

        case TK_UPDATE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = QUERY_TYPE_WRITE;
            info->operation = QUERY_OP_UPDATE;
            break;

        case TK_TRUNCATE:
            info->status = QC_QUERY_TOKENIZED;
            info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
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
                info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            }
            break;

        case TK_DEALLOCATE:
            if (info->keyword_2 == TK_PREPARE)
            {
                info->status = QC_QUERY_TOKENIZED;
                info->type_mask = QUERY_TYPE_SESSION_WRITE;
            }
            break;

        case TK_LOAD:
            if (info->keyword_2 == TK_DATA)
            {
                info->status = QC_QUERY_TOKENIZED;
                info->type_mask = QUERY_TYPE_WRITE;
                info->operation = QUERY_OP_LOAD;
            }
            break;

        case TK_RENAME:
            if (info->keyword_2 == TK_TABLE)
            {
                info->status = QC_QUERY_TOKENIZED;
                info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
            }
            break;

        case TK_START:
            switch (info->keyword_2)
            {
            case TK_TRANSACTION:
                info->status = QC_QUERY_TOKENIZED;
                info->type_mask = QUERY_TYPE_BEGIN_TRX;
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
                info->type_mask = QUERY_TYPE_SHOW_DATABASES;
                break;

            case TK_TABLES:
                info->status = QC_QUERY_TOKENIZED;
                info->type_mask = QUERY_TYPE_SHOW_TABLES;
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
    info->type_mask = QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT;

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
    info->type_mask = QUERY_TYPE_PREPARE_NAMED_STMT;

    // If information is collected in several passes, then we may
    // this information already.
    if (!info->prepare_name)
    {
        info->prepare_name = MXS_MALLOC(pName->n + 1);
        if (info->prepare_name)
        {
            memcpy(info->prepare_name, pName->z, pName->n);
            info->prepare_name[pName->n] = 0;
        }

        size_t preparable_stmt_len = pStmt->n - 2;
        size_t payload_len = 1 + preparable_stmt_len;
        size_t packet_len = MYSQL_HEADER_LEN + payload_len;

        info->preparable_stmt = gwbuf_alloc(packet_len);

        if (info->preparable_stmt)
        {
            uint8_t* ptr = GWBUF_DATA(info->preparable_stmt);
            // Payload length
            *ptr++ = payload_len;
            *ptr++ = (payload_len >> 8);
            *ptr++ = (payload_len >> 16);
            // Sequence id
            *ptr++ = 0x00;
            // Command
            *ptr++ = MYSQL_COM_QUERY;

            memcpy(ptr, pStmt->z + 1, pStmt->n - 2);
        }
    }
    else
    {
        ss_dassert(info->collect != info->collected);
        ss_dassert(strncmp(info->prepare_name, pName->z, pName->n) == 0);
    }
}

void maxscalePrivileges(Parse* pParse, int kind)
{
    QC_TRACE();

    QC_SQLITE_INFO* info = this_thread.info;
    ss_dassert(info);

    info->status = QC_QUERY_PARSED;
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);

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
    info->type_mask = 0; // Reset what was set in maxscaleKeyword

    switch (kind)
    {
    case MXS_SET_TRANSACTION:
        if ((scope == TK_GLOBAL) || (scope == TK_SESSION))
        {
            info->type_mask = QUERY_TYPE_GSYSVAR_WRITE;
        }
        else
        {
            ss_dassert(scope == 0);
            info->type_mask = QUERY_TYPE_WRITE;
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
                    info->type_mask |= QUERY_TYPE_GSYSVAR_WRITE;
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
                            info->type_mask |= QUERY_TYPE_USERVAR_WRITE;
                        }
                        else
                        {
                            info->type_mask |= QUERY_TYPE_GSYSVAR_WRITE;
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
                                    info->type_mask |= QUERY_TYPE_BEGIN_TRX;
                                    info->type_mask |= QUERY_TYPE_DISABLE_AUTOCOMMIT;
                                    break;

                                case 1:
                                    info->type_mask |= QUERY_TYPE_ENABLE_AUTOCOMMIT;
                                    info->type_mask |= QUERY_TYPE_COMMIT;
                                    break;

                                default:
                                    break;
                                }
                            }
                        }

                        if (pValue->op == TK_SELECT)
                        {
                            update_field_infos_from_select(info, pValue->x.pSelect,
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

    uint32_t u = QC_USED_IN_SELECT;

    switch (pShow->what)
    {
    case MXS_SHOW_COLUMNS:
        {
            info->type_mask = QUERY_TYPE_READ;
            update_names(info, zDatabase, zName);
            if (pShow->data == MXS_SHOW_COLUMNS_FULL)
            {
                update_field_info(info, "information_schema", "COLUMNS", "COLLATION_NAME", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_COMMENT", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_DEFAULT", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_KEY", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_NAME", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_TYPE", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "EXTRA", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "IS_NULLABLE", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "PRIVILEGES", u, NULL);
            }
            else
            {
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_DEFAULT", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_KEY", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_NAME", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "COLUMN_TYPE", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "EXTRA", u, NULL);
                update_field_info(info, "information_schema", "COLUMNS", "IS_NULLABLE", u, NULL);
            }
        }
        break;

    case MXS_SHOW_CREATE_VIEW:
        {
            info->type_mask = QUERY_TYPE_WRITE;
            update_names(info, zDatabase, zName);
        }
        break;

    case MXS_SHOW_CREATE_TABLE:
        {
            info->type_mask = QUERY_TYPE_WRITE;
            update_names(info, zDatabase, zName);
        }
        break;

    case MXS_SHOW_DATABASES:
        {
            info->type_mask = QUERY_TYPE_SHOW_DATABASES;
            update_names(info, "information_schema", "SCHEMATA");
            update_field_info(info, "information_schema", "SCHEMATA", "SCHEMA_NAME", u, NULL);
        }
        break;

    case MXS_SHOW_INDEX:
    case MXS_SHOW_INDEXES:
    case MXS_SHOW_KEYS:
        {
            info->type_mask = QUERY_TYPE_WRITE;
            update_names(info, "information_schema", "STATISTICS");
            update_field_info(info, "information_schema", "STATISTICS", "CARDINALITY", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "COLLATION", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "COLUMN_NAME", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "COMMENT", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "INDEX_COMMENT", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "INDEX_NAME", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "INDEX_TYPE", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "NON_UNIQUE", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "NULLABLE", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "PACKED", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "SEQ_IN_INDEX", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "SUB_PART", u, NULL);
            update_field_info(info, "information_schema", "STATISTICS", "TABLE_NAME", u, NULL);
        }
        break;

    case MXS_SHOW_TABLE_STATUS:
        {
            info->type_mask = QUERY_TYPE_WRITE;
            update_names(info, "information_schema", "TABLES");
            update_field_info(info, "information_schema", "TABLES", "AUTO_INCREMENT", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "AVG_ROW_LENGTH", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "CHECKSUM", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "CHECK_TIME", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "CREATE_OPTIONS", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "CREATE_TIME", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "DATA_FREE", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "DATA_LENGTH", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "ENGINE", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "INDEX_LENGTH", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "MAX_DATA_LENGTH", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "ROW_FORMAT", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "TABLE_COLLATION", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "TABLE_COMMENT", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "TABLE_NAME", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "TABLE_ROWS", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "UPDATE_TIME", u, NULL);
            update_field_info(info, "information_schema", "TABLES", "VERSION", u, NULL);
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
                info->type_mask = QUERY_TYPE_UNKNOWN;
                update_names(info, "information_schema", "SESSION_STATUS");
                update_field_info(info, "information_schema", "SESSION_STATUS", "VARIABLE_NAME", u, NULL);
                update_field_info(info, "information_schema", "SESSION_STATUS", "VARIABLE_VALUE", u, NULL);
                break;

            case MXS_SHOW_STATUS_MASTER:
                info->type_mask = QUERY_TYPE_WRITE;
                break;

            case MXS_SHOW_STATUS_SLAVE:
                info->type_mask = QUERY_TYPE_READ;
                break;

            case MXS_SHOW_STATUS_ALL_SLAVES:
                info->type_mask = QUERY_TYPE_READ;
                break;

            default:
                break;
            }
        }
        break;

    case MXS_SHOW_TABLES:
        {
            info->type_mask = QUERY_TYPE_SHOW_TABLES;
            update_names(info, "information_schema", "TABLE_NAMES");
            update_field_info(info, "information_schema", "TABLE_NAMES", "TABLE_NAME", u, NULL);
        }
        break;

    case MXS_SHOW_VARIABLES:
        {
            if (pShow->data == MXS_SHOW_VARIABLES_GLOBAL)
            {
                info->type_mask = QUERY_TYPE_GSYSVAR_READ;
            }
            else
            {
                info->type_mask = QUERY_TYPE_SYSVAR_READ;
            }
            update_names(info, "information_schema", "SESSION_VARIABLES");
            update_field_info(info, "information_schema", "SESSION_STATUS", "VARIABLE_NAME", u, NULL);
            update_field_info(info, "information_schema", "SESSION_STATUS", "VARIABLE_VALUE", u, NULL);
        }
        break;

    case MXS_SHOW_WARNINGS:
        {
            // qc_mysqliembedded claims this.
            info->type_mask = QUERY_TYPE_WRITE;
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
    info->type_mask = (QUERY_TYPE_WRITE | QUERY_TYPE_COMMIT);
    info->operation = QUERY_OP_TRUNCATE;

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
    info->type_mask = QUERY_TYPE_SESSION_WRITE;
    info->operation = QUERY_OP_CHANGE_DB;
}

/**
 * API
 */
static int32_t qc_sqlite_setup(const char* args);
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

static int32_t qc_sqlite_setup(const char* args)
{
    QC_TRACE();
    assert(!this_unit.setup);

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
                    MXS_WARNING("'%s' is not a number between %d and %d.",
                                value, QC_LOG_NOTHING, QC_LOG_NON_TOKENIZED);
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
    }

    this_unit.setup = true;
    this_unit.log_level = log_level;

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
    int rc = sqlite3_open(":memory:", &this_thread.db);
    if (rc == SQLITE_OK)
    {
        this_thread.initialized = true;

        MXS_INFO("In-memory sqlite database successfully opened for thread %lu.",
                 (unsigned long) pthread_self());

        QC_SQLITE_INFO* info = info_alloc(QC_COLLECT_ALL);

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

    ss_dassert(this_thread.db);
    int rc = sqlite3_close(this_thread.db);

    if (rc != SQLITE_OK)
    {
        MXS_WARNING("The closing of the thread specific sqlite database failed: %d, %s",
                    rc, sqlite3_errstr(rc));
    }

    this_thread.db = NULL;
    this_thread.initialized = false;
}

static int32_t qc_sqlite_parse(GWBUF* query, uint32_t collect, int32_t* result)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    QC_SQLITE_INFO* info = get_query_info(query, collect);

    if (info)
    {
        *result = info->status;
    }
    else
    {
        *result = QC_QUERY_INVALID;
    }

    return info ? QC_RESULT_OK : QC_RESULT_ERROR;
}

static int32_t qc_sqlite_get_type_mask(GWBUF* query, uint32_t* type_mask)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *type_mask = QUERY_TYPE_UNKNOWN;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_ESSENTIALS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            *type_mask = info->type_mask;
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report query type");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_operation(GWBUF* query, int32_t* op)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *op = QUERY_OP_UNDEFINED;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_ESSENTIALS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            *op = info->operation;
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report query operation");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_created_table_name(GWBUF* query, char** created_table_name)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *created_table_name = NULL;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_TABLES);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            if (info->created_table_name)
            {
                *created_table_name = MXS_STRDUP(info->created_table_name);
                MXS_ABORT_IF_NULL(created_table_name);
                rv = QC_RESULT_OK;
            }
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report created tables");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_is_drop_table_query(GWBUF* query, int32_t* is_drop_table)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *is_drop_table = 0;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_ESSENTIALS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            *is_drop_table = info->is_drop_table;
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report whether query is drop table");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_table_names(GWBUF* query,
                                         int32_t fullnames,
                                         char*** table_names,
                                         int32_t* tblsize)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *table_names = NULL;
    *tblsize = 0;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_TABLES);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            if (fullnames)
            {
                *table_names = info->table_fullnames;
            }
            else
            {
                *table_names = info->table_names;
            }

            if (*table_names)
            {
                *table_names = copy_string_array(*table_names, tblsize);
            }
            else
            {
                *tblsize = 0;
            }

            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report what tables are accessed");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_canonical(GWBUF* query, char** canonical)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *canonical = NULL;

    MXS_ERROR("qc_get_canonical not implemented yet.");

    return rv;
}

static int32_t qc_sqlite_query_has_clause(GWBUF* query, int32_t* has_clause)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *has_clause = false;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_ESSENTIALS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            *has_clause = info->has_clause;
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report whether the query has a where clause");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_database_names(GWBUF* query, char*** database_names, int* sizep)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *database_names = NULL;
    *sizep = 0;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_DATABASES);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            if (info->database_names)
            {
                *database_names = copy_string_array(info->database_names, sizep);
            }

            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report what databases are accessed");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

static int32_t qc_sqlite_get_prepare_name(GWBUF* query, char** prepare_name)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *prepare_name = NULL;
    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_ESSENTIALS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            if (info->prepare_name)
            {
                *prepare_name = MXS_STRDUP(info->prepare_name);
            }

            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report the name of a prepared statement");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

int32_t qc_sqlite_get_field_info(GWBUF* query, const QC_FIELD_INFO** infos, uint32_t* n_infos)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *infos = NULL;
    *n_infos = 0;

    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_FIELDS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            *infos = info->field_infos;
            *n_infos = info->field_infos_len;

            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report field info");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

int32_t qc_sqlite_get_function_info(GWBUF* query, const QC_FUNCTION_INFO** infos, uint32_t* n_infos)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *infos = NULL;
    *n_infos = 0;

    QC_SQLITE_INFO* info = get_query_info(query, QC_COLLECT_FUNCTIONS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            *infos = info->function_infos;
            *n_infos = info->function_infos_len;

            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(query, "cannot report field info");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

int32_t qc_sqlite_get_preparable_stmt(GWBUF* stmt, GWBUF** preparable_stmt)
{
    QC_TRACE();
    int32_t rv = QC_RESULT_ERROR;
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    *preparable_stmt = NULL;

    QC_SQLITE_INFO* info = get_query_info(stmt, QC_COLLECT_ESSENTIALS);

    if (info)
    {
        if (qc_info_is_valid(info->status))
        {
            *preparable_stmt = info->preparable_stmt;
            rv = QC_RESULT_OK;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_invalid_data(stmt, "cannot report field info");
        }
    }
    else
    {
        MXS_ERROR("The query could not be parsed. Response not valid.");
    }

    return rv;
}

/**
 * EXPORTS
 */

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
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_QUERY_CLASSIFIER,
        MXS_MODULE_BETA_RELEASE,
        QUERY_CLASSIFIER_VERSION,
        "Query classifier using sqlite.",
        "V1.0.0",
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
