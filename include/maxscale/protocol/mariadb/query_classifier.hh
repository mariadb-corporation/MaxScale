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
#include <maxscale/query_classifier.hh>

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
