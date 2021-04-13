/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <memory>

#define MXS_QUERY_CLASSIFIER_VERSION {3, 0, 0}

struct GWBUF;
struct json_t;

/**
 * qc_option_t defines options that affect the classification.
 */
enum qc_option_t
{
    QC_OPTION_STRING_ARG_AS_FIELD = (1 << 0),   /*< Report a string argument to a function as a field. */
    QC_OPTION_STRING_AS_FIELD     = (1 << 1),   /*< Report strings as fields. */
};

const uint32_t QC_OPTION_MASK = QC_OPTION_STRING_ARG_AS_FIELD | QC_OPTION_STRING_AS_FIELD;

/**
 * qc_sql_mode_t specifies what should be assumed of the statements
 * that will be parsed.
 */
enum qc_sql_mode_t
{
    QC_SQL_MODE_DEFAULT,    /*< Assume the statements are MariaDB SQL. */
    QC_SQL_MODE_ORACLE      /*< Assume the statements are PL/SQL. */
};

/**
 * @c qc_collect_info_t specifies what information should be collected during parsing.
 */
enum qc_collect_info_t
{
    QC_COLLECT_ESSENTIALS = 0x00,   /*< Collect only the base minimum. */
    QC_COLLECT_TABLES     = 0x01,   /*< Collect table names. */
    QC_COLLECT_DATABASES  = 0x02,   /*< Collect database names. */
    QC_COLLECT_FIELDS     = 0x04,   /*< Collect field information. */
    QC_COLLECT_FUNCTIONS  = 0x08,   /*< Collect function information. */

    QC_COLLECT_ALL = (QC_COLLECT_TABLES | QC_COLLECT_DATABASES | QC_COLLECT_FIELDS | QC_COLLECT_FUNCTIONS)
};
/**
 * qc_query_type_t defines bits that provide information about a
 * particular statement.
 *
 * Note that more than one bit may be set for a single statement.
 */
enum qc_query_type_t
{
    QUERY_TYPE_UNKNOWN       = 0x000000,    /*< Initial value, can't be tested bitwisely */
    QUERY_TYPE_LOCAL_READ    = 0x000001,    /*< Read non-database data, execute in MaxScale:any */
    QUERY_TYPE_READ          = 0x000002,    /*< Read database data:any */
    QUERY_TYPE_WRITE         = 0x000004,    /*< Master data will be  modified:master */
    QUERY_TYPE_MASTER_READ   = 0x000008,    /*< Read from the master:master */
    QUERY_TYPE_SESSION_WRITE = 0x000010,    /*< Session data will be modified:master or all */
    QUERY_TYPE_USERVAR_WRITE = 0x000020,    /*< Write a user variable:master or all */
    QUERY_TYPE_USERVAR_READ  = 0x000040,    /*< Read a user variable:master or any */
    QUERY_TYPE_SYSVAR_READ   = 0x000080,    /*< Read a system variable:master or any */
    /** Not implemented yet */
    // QUERY_TYPE_SYSVAR_WRITE       = 0x000100, /*< Write a system variable:master or all */
    QUERY_TYPE_GSYSVAR_READ       = 0x000200,   /*< Read global system variable:master or any */
    QUERY_TYPE_GSYSVAR_WRITE      = 0x000400,   /*< Write global system variable:master or all */
    QUERY_TYPE_BEGIN_TRX          = 0x000800,   /*< BEGIN or START TRANSACTION */
    QUERY_TYPE_ENABLE_AUTOCOMMIT  = 0x001000,   /*< SET autocommit=1 */
    QUERY_TYPE_DISABLE_AUTOCOMMIT = 0x002000,   /*< SET autocommit=0 */
    QUERY_TYPE_ROLLBACK           = 0x004000,   /*< ROLLBACK */
    QUERY_TYPE_COMMIT             = 0x008000,   /*< COMMIT */
    QUERY_TYPE_PREPARE_NAMED_STMT = 0x010000,   /*< Prepared stmt with name from user:all */
    QUERY_TYPE_PREPARE_STMT       = 0x020000,   /*< Prepared stmt with id provided by server:all */
    QUERY_TYPE_EXEC_STMT          = 0x040000,   /*< Execute prepared statement:master or any */
    QUERY_TYPE_CREATE_TMP_TABLE   = 0x080000,   /*< Create temporary table:master (could be all) */
    QUERY_TYPE_READ_TMP_TABLE     = 0x100000,   /*< Read temporary table:master (could be any) */
    QUERY_TYPE_SHOW_DATABASES     = 0x200000,   /*< Show list of databases */
    QUERY_TYPE_SHOW_TABLES        = 0x400000,   /*< Show list of tables */
    QUERY_TYPE_DEALLOC_PREPARE    = 0x1000000   /*< Dealloc named prepare stmt:all */
};

/**
 * qc_query_op_t defines the operations a particular statement can perform.
 */
enum qc_query_op_t
{
    QUERY_OP_UNDEFINED = 0,

    QUERY_OP_ALTER,
    QUERY_OP_CALL,
    QUERY_OP_CHANGE_DB,
    QUERY_OP_CREATE,
    QUERY_OP_DELETE,
    QUERY_OP_DROP,
    QUERY_OP_EXECUTE,
    QUERY_OP_EXPLAIN,
    QUERY_OP_GRANT,
    QUERY_OP_INSERT,
    QUERY_OP_LOAD_LOCAL,
    QUERY_OP_LOAD,
    QUERY_OP_REVOKE,
    QUERY_OP_SELECT,
    QUERY_OP_SET,
    QUERY_OP_SHOW,
    QUERY_OP_TRUNCATE,
    QUERY_OP_UPDATE,
};

/**
 * qc_parse_result_t defines the possible outcomes when a statement is parsed.
 */
enum qc_parse_result_t
{
    QC_QUERY_INVALID          = 0,  /*< The query was not recognized or could not be parsed. */
    QC_QUERY_TOKENIZED        = 1,  /*< The query was classified based on tokens; incompletely classified. */
    QC_QUERY_PARTIALLY_PARSED = 2,  /*< The query was only partially parsed; incompletely classified. */
    QC_QUERY_PARSED           = 3   /*< The query was fully parsed; completely classified. */
};

/**
 * qc_field_context_t defines the context where a field appears.
 *
 * NOTE: A particular bit does NOT mean that the field appears ONLY in the context,
 *       but it may appear in other contexts as well.
 */
typedef enum qc_field_context
{
    QC_FIELD_UNION    = 1,  /** The field appears on the right hand side in a UNION. */
    QC_FIELD_SUBQUERY = 2   /** The field appears in a subquery. */
} qc_field_context_t;

struct QC_FIELD_INFO
{
    char*    database;  /** Present if the field is of the form "a.b.c", NULL otherwise. */
    char*    table;     /** Present if the field is of the form "a.b", NULL otherwise. */
    char*    column;    /** Always present. */
    uint32_t context;   /** The context in which the field appears. */
};

/**
 * QC_FUNCTION_INFO contains information about a function used in a statement.
 */
struct QC_FUNCTION_INFO
{
    char*          name;    /** Name of function. */
    QC_FIELD_INFO* fields;  /** What fields the function accesses. */
    uint32_t       n_fields;/** The number of fields in @c fields. */
};

/**
 * Each API function returns @c QC_RESULT_OK if the actual parsing process
 * succeeded, and some error code otherwise.
 */
enum qc_result_t
{
    QC_RESULT_OK,
    QC_RESULT_ERROR
};

/**
 * QC_STMT_INFO is an opaque type where the query classifier stores
 * information about a statement.
 */
struct QC_STMT_INFO
{
};

/**
 * QC_STMT_RESULT contains limited information about a particular
 * statement.
 */
struct QC_STMT_RESULT
{
    qc_parse_result_t status;
    uint32_t          type_mask;
    qc_query_op_t     op;
};

/**
 * Parses the statement in the provided buffer and returns a value specifying
 * to what extent the statement could be parsed.
 *
 * There is no need to call this function explicitly before calling any of
 * the other functions; e.g. qc_get_type_mask(). When some particular property of
 * a statement is asked for, the statement will be parsed if it has not been
 * parsed yet. Also, if the statement in the provided buffer has been parsed
 * already then this function will only return the result of that parsing;
 * the statement will not be parsed again.
 *
 * @param stmt     A buffer containing an COM_QUERY or COM_STMT_PREPARE packet.
 * @param collect  A bitmask of @c qc_collect_info_t values. Specifies what information
 *                 should be collected.
 *
 *                 Note that this is merely a hint and does not restrict what
 *                 information can be queried for. If necessary, the statement
 *                 will transparently be reparsed.
 *
 * @return To what extent the statement could be parsed.
 */
qc_parse_result_t qc_parse(GWBUF* stmt, uint32_t collect);

/**
 * Returns information about affected fields.
 *
 * @param stmt     A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
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
 * Returns information about function usage.
 *
 * @param stmt     A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 * @param infos    Pointer to pointer that after the call will point to an
 *                 array of QC_FUNCTION_INFO:s.
 * @param n_infos  Pointer to size_t variable where the number of items
 *                 in @c infos will be returned.
 *
 * @note The returned array belongs to the GWBUF and remains valid for as
 *       long as the GWBUF is valid. If the data is needed for longer than
 *       that, it must be copied.
 *
 * @note For each function, only the fields that any invocation of it directly
 *       accesses will be returned. For instance:
 *
 *           select length(a), length(concat(b, length(a))) from t
 *
 *       will for @length return the field @a and for @c concat the field @b.
 */
void qc_get_function_info(GWBUF* stmt, const QC_FUNCTION_INFO** infos, size_t* n_infos);

/**
 * Returns the name of the created table.
 *
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
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
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return Vector of strings
 */
std::vector<std::string> qc_get_database_names(GWBUF* stmt);

/**
 * Returns the operation of the statement.
 *
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return The operation of the statement.
 */
qc_query_op_t qc_get_operation(GWBUF* stmt);

/**
 * Returns the name of the prepared statement, if the statement
 * is a PREPARE or EXECUTE statement.
 *
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
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
 * Returns the preparable statement of a PREPARE statment. Other query classifier
 * functions can then be used on the returned statement to find out information
 * about the preparable statement. The returned @c GWBUF should not be used for
 * anything else but for obtaining information about the preparable statement.
 *
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return The preparable statement, if @stmt was a COM_QUERY PREPARE statement,
 *         or NULL.
 *
 * @attention If the packet was a COM_STMT_PREPARE, then this function will
 *            return NULL and the actual properties of the query can be obtained
 *            by calling any of the qc-functions directly on the GWBUF containing
 *            the COM_STMT_PREPARE. However, the type mask will contain the
 *            bit @c QUERY_TYPE_PREPARE_STMT.
 *
 * @attention The returned @c GWBUF is the property of @c stmt and will be
 *            deleted along with it.
 */
GWBUF* qc_get_preparable_stmt(GWBUF* stmt);

/**
 * Returns the tables accessed by the statement.
 *
 * @param stmt       A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 * @param tblsize    Pointer to integer where the number of tables is stored.
 * @param fullnames  If true, a table names will include the database name
 *                   as well (if explicitly referred to in the statement).
 *
 * @return Array of strings or NULL if a memory allocation fails.
 *
 * @note The returned array and the strings pointed to @b must be freed
 *       by the caller.
 */
std::vector<std::string> qc_get_table_names(GWBUF* stmt, bool fullnames);

/**
 * Returns a bitmask specifying the type(s) of the statement. The result
 * should be tested against specific qc_query_type_t values* using the
 * bitwise & operator, never using the == operator.
 *
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return A bitmask with the type(s) the query.
 *
 * @see qc_query_is_type
 */
uint32_t qc_get_type_mask(GWBUF* stmt);

/**
 * Returns the type bitmask of transaction related statements.
 *
 * If the statement starts a transaction, ends a transaction or
 * changes the autocommit state, the returned bitmap will be a
 * combination of:
 *
 *    QUERY_TYPE_BEGIN_TRX
 *    QUERY_TYPE_COMMIT
 *    QUERY_TYPE_ROLLBACK
 *    QUERY_TYPE_ENABLE_AUTOCOMMIT
 *    QUERY_TYPE_DISABLE_AUTOCOMMIT
 *    QUERY_TYPE_READ  (explicitly read only transaction)
 *    QUERY_TYPE_WRITE (explicitly read write transaction)
 *
 * Otherwise the result will be 0.
 *
 * @param stmt A COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return The relevant type bits if the statement is transaction
 *         related, otherwise 0.
 */
uint32_t qc_get_trx_type_mask(GWBUF* stmt);

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
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return True, if the statement has a WHERE or USING clause, false
 *         otherwise.
 */
bool qc_query_has_clause(GWBUF* stmt);

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

/**
 * Gets the options of the *calling* thread.
 *
 * @return Bit mask of values from qc_option_t.
 */
uint32_t qc_get_options();

/**
 * Sets the options for the *calling* thread.
 *
 * @param options Bits from qc_option_t.
 *
 * @return true if the options were valid, false otherwise.
 */
bool qc_set_options(uint32_t options);

enum qc_trx_parse_using_t
{
    QC_TRX_PARSE_USING_QC,      /**< Use the query classifier. */
    QC_TRX_PARSE_USING_PARSER,  /**< Use custom parser. */
};

/**
 * Returns the type bitmask of transaction related statements.
 *
 * @param stmt  A COM_QUERY or COM_STMT_PREPARE packet.
 * @param use   What method should be used.
 *
 * @return The relevant type bits if the statement is transaction
 *         related, otherwise 0.
 *
 * @see qc_get_trx_type_mask
 */
uint32_t qc_get_trx_type_mask_using(GWBUF* stmt, qc_trx_parse_using_t use);

/**
 * Gets the sql mode of the *calling* thread.
 *
 * @return The mode.
 */
qc_sql_mode_t qc_get_sql_mode();

/**
 * Sets the sql mode for the *calling* thread.
 *
 * @param sql_mode  The mode.
 */
void qc_set_sql_mode(qc_sql_mode_t sql_mode);

/**
 * Returns whether the statement is a DROP TABLE statement.
 *
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return True if the statement is a DROP TABLE statement, false otherwise.
 *
 * @todo This function is far too specific.
 */
bool qc_is_drop_table_query(GWBUF* stmt);

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
 * Set the version of the server. The version may affect how a statement
 * is classified. Note that the server version is maintained separately
 * for each thread.
 *
 * @param version  Version encoded as MariaDB encodes the version, i.e.:
 *                 version = major * 10000 + minor * 100 + patch
 */
void qc_set_server_version(uint64_t version);

/**
 * Get the thread specific version assumed of the server. If the version has
 * not been set, all values are 0.
 *
 * @return The version as MariaDB encodes the version, i.e:
 *         version = major * 10000 + minor * 100 + patch
 */
uint64_t qc_get_server_version();

/**
 * String represenation for the parse result.
 *
 * @param result A parsing result.
 *
 * @return The corresponding string.
 */
const char* qc_result_to_string(qc_parse_result_t result);

/**
 * Classify statement
 *
 * @param zHost      The MaxScale host.
 * @param statement  The statement to be classified.
 *
 * @return A json object containing information about the statement.
 */
std::unique_ptr<json_t> qc_classify_as_json(const char* zHost, const std::string& statement);
