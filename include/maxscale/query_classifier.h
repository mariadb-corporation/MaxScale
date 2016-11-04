#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>
#include <maxscale/buffer.h>

MXS_BEGIN_DECLS

#define QUERY_CLASSIFIER_VERSION {1, 1, 0}

/**
 * qc_query_type_t defines bits that provide information about a
 * particular statement.
 *
 * Note that more than one bit may be set for a single statement.
 */
typedef enum qc_query_type
{
    QUERY_TYPE_UNKNOWN            = 0x000000, /*< Initial value, can't be tested bitwisely */
    QUERY_TYPE_LOCAL_READ         = 0x000001, /*< Read non-database data, execute in MaxScale:any */
    QUERY_TYPE_READ               = 0x000002, /*< Read database data:any */
    QUERY_TYPE_WRITE              = 0x000004, /*< Master data will be  modified:master */
    QUERY_TYPE_MASTER_READ        = 0x000008, /*< Read from the master:master */
    QUERY_TYPE_SESSION_WRITE      = 0x000010, /*< Session data will be modified:master or all */
    /** Not implemented yet */
    //QUERY_TYPE_USERVAR_WRITE      = 0x000020, /*< Write a user variable:master or all */
    QUERY_TYPE_USERVAR_READ       = 0x000040, /*< Read a user variable:master or any */
    QUERY_TYPE_SYSVAR_READ        = 0x000080, /*< Read a system variable:master or any */
    /** Not implemented yet */
    //QUERY_TYPE_SYSVAR_WRITE       = 0x000100, /*< Write a system variable:master or all */
    QUERY_TYPE_GSYSVAR_READ       = 0x000200, /*< Read global system variable:master or any */
    QUERY_TYPE_GSYSVAR_WRITE      = 0x000400, /*< Write global system variable:master or all */
    QUERY_TYPE_BEGIN_TRX          = 0x000800, /*< BEGIN or START TRANSACTION */
    QUERY_TYPE_ENABLE_AUTOCOMMIT  = 0x001000, /*< SET autocommit=1 */
    QUERY_TYPE_DISABLE_AUTOCOMMIT = 0x002000, /*< SET autocommit=0 */
    QUERY_TYPE_ROLLBACK           = 0x004000, /*< ROLLBACK */
    QUERY_TYPE_COMMIT             = 0x008000, /*< COMMIT */
    QUERY_TYPE_PREPARE_NAMED_STMT = 0x010000, /*< Prepared stmt with name from user:all */
    QUERY_TYPE_PREPARE_STMT       = 0x020000, /*< Prepared stmt with id provided by server:all */
    QUERY_TYPE_EXEC_STMT          = 0x040000, /*< Execute prepared statement:master or any */
    QUERY_TYPE_CREATE_TMP_TABLE   = 0x080000, /*< Create temporary table:master (could be all) */
    QUERY_TYPE_READ_TMP_TABLE     = 0x100000, /*< Read temporary table:master (could be any) */
    QUERY_TYPE_SHOW_DATABASES     = 0x200000, /*< Show list of databases */
    QUERY_TYPE_SHOW_TABLES        = 0x400000  /*< Show list of tables */
} qc_query_type_t;

/**
 * qc_query_op_t defines the operations a particular statement can perform.
 */
typedef enum qc_query_op
{
    QUERY_OP_UNDEFINED     = 0,
    QUERY_OP_SELECT        = (1 << 0),
    QUERY_OP_UPDATE        = (1 << 1),
    QUERY_OP_INSERT        = (1 << 2),
    QUERY_OP_DELETE        = (1 << 3),
    QUERY_OP_TRUNCATE      = (1 << 4),
    QUERY_OP_ALTER         = (1 << 5),
    QUERY_OP_CREATE        = (1 << 6),
    QUERY_OP_DROP          = (1 << 7),
    QUERY_OP_CHANGE_DB     = (1 << 8),
    QUERY_OP_LOAD          = (1 << 9),
    QUERY_OP_GRANT         = (1 << 10),
    QUERY_OP_REVOKE        = (1 << 11)
} qc_query_op_t;

/**
 * qc_parse_result_t defines the possible outcomes when a statement is parsed.
 */
typedef enum qc_parse_result
{
    QC_QUERY_INVALID          = 0, /*< The query was not recognized or could not be parsed. */
    QC_QUERY_TOKENIZED        = 1, /*< The query was classified based on tokens; incompletely classified. */
    QC_QUERY_PARTIALLY_PARSED = 2, /*< The query was only partially parsed; incompletely classified. */
    QC_QUERY_PARSED           = 3  /*< The query was fully parsed; completely classified. */
} qc_parse_result_t;


/**
 * QC_FIELD_INFO contains information about a field used in a statement.
 */
typedef struct qc_field_info
{
    char* database;  /** Present if the field is of the form "a.b.c", NULL otherwise. */
    char* table;     /** Present if the field is of the form "a.b", NULL otherwise. */
    char* column;    /** Always present. */
    // TODO: Possibly add bits telling where the field is used; e.g. in the select
    // TODO: part or the where part, or both.
} QC_FIELD_INFO;

/**
 * QUERY_CLASSIFIER defines the object a query classifier plugin must
 * implement and return.
 *
 * To a user of the query classifier functionality, it can in general
 * be ignored.
 */
typedef struct query_classifier
{
    bool (*qc_init)(const char* args);
    void (*qc_end)(void);

    bool (*qc_thread_init)(void);
    void (*qc_thread_end)(void);

    qc_parse_result_t (*qc_parse)(GWBUF* stmt);

    uint32_t (*qc_get_type)(GWBUF* stmt);
    qc_query_op_t (*qc_get_operation)(GWBUF* stmt);

    char* (*qc_get_created_table_name)(GWBUF* stmt);
    bool (*qc_is_drop_table_query)(GWBUF* stmt);
    bool (*qc_is_real_query)(GWBUF* stmt);
    char** (*qc_get_table_names)(GWBUF* stmt, int* tblsize, bool fullnames);
    char* (*qc_get_canonical)(GWBUF* stmt);
    bool (*qc_query_has_clause)(GWBUF* stmt);
    char* (*qc_get_affected_fields)(GWBUF* stmt);
    char** (*qc_get_database_names)(GWBUF* stmt, int* size);
    char* (*qc_get_prepare_name)(GWBUF* stmt);
    qc_query_op_t (*qc_get_prepare_operation)(GWBUF* stmt);
    void (*qc_get_field_info)(GWBUF* stmt, const QC_FIELD_INFO** infos, size_t* n_infos);
} QUERY_CLASSIFIER;

/**
 * Loads and initializes the default query classifier.
 *
 * This must be called once during the execution of a process. The query
 * classifier functions can only be used if this function returns true.
 * MaxScale calls this function, so plugins should not do that.
 *
 * @param plugin_name  The name of the plugin from which the query classifier
 *                     should be loaded.
 * @param plugin_args  The arguments to be provided to the query classifier.
 *
 * @return True if the query classifier could be loaded and initialized,
 *         false otherwise.
 *
 * @see qc_end qc_thread_init
 */
bool qc_init(const char* plugin_name, const char* plugin_args);

/**
 * Finalizes and unloads the query classifier.
 *
 * A successful call of qc_init() should before program exit be followed
 * by a call to this function. MaxScale calls this function, so plugins
 * should not do that.
 *
 * @see qc_init qc_thread_end
 */
void qc_end(void);

/**
 * Loads a particular query classifier.
 *
 * In general there is no need to use this function, but rely upon qc_init().
 * However, if there is a need to use multiple query classifiers concurrently
 * then this function provides the means for that. Note that after a query
 * classifier has been loaded, it must explicitly be initialized before it
 * can be used.
 *
 * @param plugin_name  The name of the plugin from which the query classifier
 *                     should be loaded.
 *
 * @return A QUERY_CLASSIFIER object if successful, NULL otherwise.
 *
 * @see qc_unload
 */
QUERY_CLASSIFIER* qc_load(const char* plugin_name);

/**
 * Unloads an explicitly loaded query classifier.
 *
 * @see qc_load
 */
void qc_unload(QUERY_CLASSIFIER* classifier);

/**
 * Performs thread initialization needed by the query classifier.
 * Should be called in every thread, except the one where qc_init()
 * was called. MaxScale calls this function, so plugins should not
 * do that.
 *
 * @return True if the initialization succeeded, false otherwise.
 *
 * @see qc_thread_end
 */
bool qc_thread_init(void);

/**
 * Performs thread finalization needed by the query classifier.
 * A successful call to qc_thread_init() should at some point be followed
 * by a call to this function. MaxScale calls this function, so plugins
 * should not do that.
 *
 * @see qc_thread_init
 */
void qc_thread_end(void);

/**
 * Parses the statement in the provided buffer and returns a value specifying
 * to what extent the statement could be parsed.
 *
 * There is no need to call this function explicitly before calling any of
 * the other functions; e.g. qc_get_type(). When some particular property of
 * a statement is asked for, the statement will be parsed if it has not been
 * parsed yet. Also, if the statement in the provided buffer has been parsed
 * already then this function will only return the result of that parsing;
 * the statement will not be parsed again.
 *
 * @param stmt  A buffer containing an COM_QUERY packet.
 *
 * @return To what extent the statement could be parsed.
 */
qc_parse_result_t qc_parse(GWBUF* stmt);

/**
 * Returns the fields the statement affects, as a string of names separated
 * by spaces. Note that the fields do not contain any table information.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return A string containing the fields or NULL if a memory allocation
 *         failure occurs. The string must be freed by the caller.
 */
char* qc_get_affected_fields(GWBUF* stmt);

/**
 * Returns information about affected fields.
 *
 * @param stmt     A buffer containing a COM_QUERY packet.
 * @param infos    Pointer to pointer that after the call will point to an
 *                 array of QC_FIELD_INFO:s.
 * @param n_infos  Pointer to size_t variable where the number of items
 *                 in @c infos will be returned.
 *
 * @note The returned array belongs to the GWBUF and remains valid for as
 *       long as the GWBUF is valid. If the data is needed for longer than
 *       that, it must be copied.
 */
void qc_get_field_info(GWBUF* stmt, const QC_FIELD_INFO** infos, size_t* n_infos);

/**
 * Returns the statement, with literals replaced with question marks.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return A statement in its canonical form, or NULL if a memory
 *         allocation fails. The string must be freed by the caller.
 */
char* qc_get_canonical(GWBUF* stmt);

/**
 * Returns the name of the created table.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return The name of the created table or NULL if the statement
 *         does not create a table or a memory allocation failed.
 *         The string must be freed by the caller.
 */
char* qc_get_created_table_name(GWBUF* stmt);

/**
 * Returns the databases accessed by the statement. Note that a
 * possible default database is not returned.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 * @param size  Pointer to integer where the number of databases
 *              is stored.
 *
 * @return Array of strings or NULL if a memory allocation fails.
 *
 * @note The returned array and the strings pointed to @b must be freed
 *       by the caller.
 */
char** qc_get_database_names(GWBUF* stmt, int* size);

/**
 * Returns the operation of the statement.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return The operation of the statement.
 */
qc_query_op_t qc_get_operation(GWBUF* stmt);

/**
 * Returns the name of the prepared statement, if the statement
 * is a PREPARE or EXECUTE statement.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return The name of the prepared statement, if the statement
 *         is a PREPARE or EXECUTE statement; otherwise NULL.
 *
 * @note The returned string @b must be freed by the caller.
 *
 * @note Even though a COM_STMT_PREPARE can be given to the query
 *       classifier for parsing, this function will in that case
 *       return NULL since the id of the statement is provided by
 *       the server.
 */
char* qc_get_prepare_name(GWBUF* stmt);

/**
 * Returns the operator of the prepared statement, if the statement
 * is a PREPARE statement.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return The operator of the prepared statement, if the statement
 *         is a PREPARE statement; otherwise QUERY_OP_UNDEFINED.
 */
qc_query_op_t qc_get_prepare_operation(GWBUF* stmt);

/**
 * Returns the tables accessed by the statement.
 *
 * @param stmt       A buffer containing a COM_QUERY packet.
 * @param tblsize    Pointer to integer where the number of tables is stored.
 * @param fullnames  If true, a table names will include the database name
 *                   as well (if explicitly referred to in the statement).
 *
 * @return Array of strings or NULL if a memory allocation fails.
 *
 * @note The returned array and the strings pointed to @b must be freed
 *       by the caller.
 */
char** qc_get_table_names(GWBUF* stmt, int* size, bool fullnames);


/**
 * Returns a bitmask specifying the type(s) of the statement. The result
 * should be tested against specific qc_query_type_t values* using the
 * bitwise & operator, never using the == operator.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return A bitmask with the type(s) the query.
 *
 * @see qc_query_is_type
 */
uint32_t qc_get_type(GWBUF* stmt);

/**
 * Returns whether the statement is a DROP TABLE statement.
 *
 * @param stmt  A buffer containing a COM_QUERY packet.
 *
 * @return True if the statement is a DROP TABLE statement, false otherwise.
 *
 * @todo This function is far too specific.
 */
bool qc_is_drop_table_query(GWBUF* stmt);

/**
 * Returns whether the statement is a "real" statement. Statements that affect
 * the underlying database are considered real statements, while statements that
 * target specific rows or variable data are regarded as real statement. That is,
 * a real statement is  SELECT, UPDATE, INSERT, DELETE or a variation thereof.
 *
 * @param stmt  A buffer containing a COM_QUERY.
 *
 * @return True if the statement is a real query, false otherwise.
 *
 * @todo Consider whether the function name should be changed or the function
 *       removed altogether.
 */
bool qc_is_real_query(GWBUF* stmt);

/**
 * Returns the string representation of a query operation.
 *
 * @param op  A query operation.
 *
 * @return The corresponding string.
 *
 * @note The returned string is statically allocated and must *not* be freed.
 */
const char* qc_op_to_string(qc_query_op_t op);

/**
 * Returns whether the typemask contains a particular type.
 *
 * @param typemask  A bitmask of query types.
 * @param type      A particular qc_query_type_t value.
 *
 * @return True, if the type is in the mask.
 */
static inline bool qc_query_is_type(uint32_t typemask, qc_query_type_t type)
{
    return (typemask & (uint32_t)type) == (uint32_t)type;
}

/**
 * Returns whether the statement has a WHERE or a USING clause.
 *
 * @param stmt  A buffer containing a COM_QUERY.
 *
 * @return True, if the statement has a WHERE or USING clause, false
 *         otherwise.
 */
bool qc_query_has_clause(GWBUF* stmt);

/**
 * Returns the string representation of a query type.
 *
 * @param type  A query type (not a bitmask of several).
 *
 * @return The corresponding string.
 *
 * @note The returned string is statically allocated and must @b not be freed.
 */
const char* qc_type_to_string(qc_query_type_t type);

/**
 * Returns a string representation of a type bitmask.
 *
 * @param typemask  A bit mask of query types.
 *
 * @return The corresponding string or NULL if the allocation fails.
 *
 * @note The returned string is dynamically allocated and @b must be freed.
 */
char* qc_typemask_to_string(uint32_t typemask);

MXS_END_DECLS
