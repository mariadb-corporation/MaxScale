/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/buffer.hh>

#define MXS_QUERY_CLASSIFIER_VERSION {3, 0, 0}

class GWBUF;
struct json_t;

/**
 * QC_CACHE_STATS provides statistics of the cache.
 */
struct QC_CACHE_STATS
{
    int64_t size;       /** The current size of the cache. */
    int64_t inserts;    /** The number of inserts. */
    int64_t hits;       /** The number of hits. */
    int64_t misses;     /** The number of misses. */
    int64_t evictions;  /** The number of evictions. */
};

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
    QUERY_TYPE_UNKNOWN            = 0,      /*< Initial value, can't be tested bitwisely */
    QUERY_TYPE_LOCAL_READ         = 1 << 0, /*< Read non-database data, execute in MaxScale:any */
    QUERY_TYPE_READ               = 1 << 1, /*< Read database data:any */
    QUERY_TYPE_WRITE              = 1 << 2, /*< Master data will be  modified:master */
    QUERY_TYPE_MASTER_READ        = 1 << 3, /*< Read from the master:master */
    QUERY_TYPE_SESSION_WRITE      = 1 << 4, /*< Session data will be modified:master or all */
    QUERY_TYPE_USERVAR_WRITE      = 1 << 5, /*< Write a user variable:master or all */
    QUERY_TYPE_USERVAR_READ       = 1 << 6, /*< Read a user variable:master or any */
    QUERY_TYPE_SYSVAR_READ        = 1 << 7, /*< Read a system variable:master or any */
    QUERY_TYPE_GSYSVAR_READ       = 1 << 8, /*< Read global system variable:master or any */
    QUERY_TYPE_GSYSVAR_WRITE      = 1 << 9, /*< Write global system variable:master or all */
    QUERY_TYPE_BEGIN_TRX          = 1 << 10,/*< BEGIN or START TRANSACTION */
    QUERY_TYPE_ENABLE_AUTOCOMMIT  = 1 << 11,/*< SET autocommit=1 */
    QUERY_TYPE_DISABLE_AUTOCOMMIT = 1 << 12,/*< SET autocommit=0 */
    QUERY_TYPE_ROLLBACK           = 1 << 13,/*< ROLLBACK */
    QUERY_TYPE_COMMIT             = 1 << 14,/*< COMMIT */
    QUERY_TYPE_PREPARE_NAMED_STMT = 1 << 15,/*< Prepared stmt with name from user:all */
    QUERY_TYPE_PREPARE_STMT       = 1 << 16,/*< Prepared stmt with id provided by server:all */
    QUERY_TYPE_EXEC_STMT          = 1 << 17,/*< Execute prepared statement:master or any */
    QUERY_TYPE_CREATE_TMP_TABLE   = 1 << 18,/*< Create temporary table:master (could be all) */
    QUERY_TYPE_READ_TMP_TABLE     = 1 << 19,/*< Read temporary table:master (could be any) */
    QUERY_TYPE_SHOW_DATABASES     = 1 << 20,/*< Show list of databases */
    QUERY_TYPE_SHOW_TABLES        = 1 << 21,/*< Show list of tables */
    QUERY_TYPE_DEALLOC_PREPARE    = 1 << 22,/*< Dealloc named prepare stmt:all */
    QUERY_TYPE_READONLY           = 1 << 23,/*< The READ ONLY part of SET TRANSACTION */
    QUERY_TYPE_READWRITE          = 1 << 24,/*< The READ WRITE part of SET TRANSACTION  */
    QUERY_TYPE_NEXT_TRX           = 1 << 25,/*< SET TRANSACTION that's only for the next transaction */
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
    QUERY_OP_SET_TRANSACTION,
    QUERY_OP_SHOW,
    QUERY_OP_TRUNCATE,
    QUERY_OP_UPDATE,
    QUERY_OP_KILL,
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
    std::string_view database;  /** Present if the field is of the form "a.b.c", empty otherwise. */
    std::string_view table;     /** Present if the field is of the form "a.b", empty otherwise. */
    std::string_view column;    /** Always present. */
    uint32_t         context;   /** The context in which the field appears. */
};

/**
 * QC_FUNCTION_INFO contains information about a function used in a statement.
 */
struct QC_FUNCTION_INFO
{
    std::string_view name;    /** Name of function. */
    QC_FIELD_INFO*   fields;  /** What fields the function accesses. */
    uint32_t         n_fields;/** The number of fields in @c fields. */
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
 * QC_STMT_RESULT contains limited information about a particular
 * statement.
 */
struct QC_STMT_RESULT
{
    qc_parse_result_t status;
    uint32_t          type_mask;
    qc_query_op_t     op;
};

enum qc_kill_type_t
{
    QC_KILL_CONNECTION,
    QC_KILL_QUERY,
    QC_KILL_QUERY_ID
};

/**
 * Public interface to query classifier cache state.
 */
struct QC_CACHE_ENTRY
{
    int64_t        hits;
    QC_STMT_RESULT result;
};

/**
 * QC_CACHE_PROPERTIES specifies the limits of the query classification cache.
 */
struct QC_CACHE_PROPERTIES
{
    int64_t max_size;   /** The maximum size of the cache. */
};

/**
 * Contains the information about a KILL command.
 */
struct QC_KILL
{
    std::string    target;                      // The string form target of the KILL
    bool           user = false;                // If true, the the value in `target` is the name of a user.
    bool           soft = false;                // If true, the SOFT option was used
    qc_kill_type_t type = QC_KILL_CONNECTION;   // Type of the KILL command
};

/**
 * Returns the tables accessed by the statement.
 *
 * @param stmt  A buffer containing a COM_QUERY or COM_STMT_PREPARE packet.
 *
 * @return Array of strings or NULL if a memory allocation fails.
 *
 * @note The returned array and the strings pointed to @b must be freed
 *       by the caller.
 */
struct QcTableName
{
    QcTableName() = default;

    QcTableName(std::string_view table)
        : table(table)
    {
    }

    QcTableName(std::string_view db, std::string_view table)
        : db(db)
        , table(table)
    {
    }

    std::string_view db;
    std::string_view table;

    bool operator == (const QcTableName& rhs) const
    {
        return this->db == rhs.db && this->table == rhs.table;
    }

    bool operator < (const QcTableName& rhs) const
    {
        return this->db < rhs.db || (this->db == rhs.db && this->table < rhs.table);
    }

    bool empty() const
    {
        return this->db.empty() && this->table.empty();
    }

    std::string to_string() const
    {
        std::string s;

        if (!this->db.empty())
        {
            s = this->db;
            s += ".";
        }

        s += table;

        return s;
    }
};

inline std::ostream& operator << (std::ostream& out, const QcTableName& x)
{
    out << x.to_string();
    return out;
}

enum qc_trx_parse_using_t
{
    QC_TRX_PARSE_USING_QC,      /**< Use the query classifier. */
    QC_TRX_PARSE_USING_PARSER,  /**< Use custom parser. */
};

namespace maxscale
{

namespace parser
{

const char* to_string(qc_parse_result_t result);
const char* to_string(qc_kill_type_t type);

}

class Parser
{
public:
    using TableNames = std::vector<QcTableName>;
    using DatabaseNames = std::vector<std::string_view>;

    /**
     * Plugin defines the object a parser plugin must
     * implement and return.
     */
    class Plugin
    {
    public:
        /**
         * Must be called once to setup the parser plugin.
         *
         * @param sql_mode  The default sql mode.
         * @param args      The value of `query_classifier_args` in the configuration file.
         *
         * @return True, if the parser plugin be setup, otherwise false.
         */
        virtual bool setup(qc_sql_mode_t sql_mode, const char* args) = 0;

        /**
         * Must be called once per thread where the parser will be used. Note that
         * this will automatically be done in all MaxScale routing threads.
         *
         * @return True, if the thread initialization succeeded, otherwise false.
         */
        virtual bool thread_init(void) = 0;

        /**
         * Must be called once when a thread finishes. Note that this will
         * automatically be done in all MaxScale routing threads.
         */
        virtual void thread_end(void) = 0;

        /**
         * Return statement currently being classified.
         *
         * @param ppStmp  Pointer to pointer that on return will point to the
         *                statement being classified.
         * @param pLen    Pointer to value that on return will contain the length
         *                of the returned string.
         *
         * @return True, if a statement was returned (i.e. a statement is being
         *         classified), otherwise false.
         */
        virtual bool get_current_stmt(const char** ppStmt, size_t* pLen) = 0;

        /**
         * Get result from info.
         *
         * @param  The info whose result should be returned.
         *
         * @return The result of the provided info.
         */
        virtual QC_STMT_RESULT get_result_from_info(const QC_STMT_INFO* info) = 0;

        /**
         * Get canonical statement
         *
         * @param info  The info whose canonical statement should be returned.
         *
         * @attention - The string_view refers to data that remains valid only as long
         *              as @c info remains valid.
         *            - If @c info is of a COM_STMT_PREPARE, then the canonical string will
         *              be suffixed by ":P".
         *
         * @return The canonical statement.
         */
        virtual std::string_view info_get_canonical(const QC_STMT_INFO* info) = 0;

        virtual mxs::Parser& parser() = 0;
    };

    virtual ~Parser() = default;

    static bool type_mask_contains(uint32_t type_mask, qc_query_type_t type)
    {
        return (type_mask & (uint32_t)type) == (uint32_t)type;
    }

    static std::string type_mask_to_string(uint32_t type_mask);

    static const char* op_to_string(qc_query_op_t op);

    static Plugin* load(const char* zPlugin_name);
    static void    unload(Plugin* pPlugin);

    virtual Plugin& plugin() const = 0;

    virtual qc_parse_result_t parse(GWBUF* pStmt, uint32_t collect) const = 0;
    std::unique_ptr<json_t>   parse_to_resource(const char* zHost, const std::string& statement) const;

    virtual GWBUF            create_buffer(const std::string& statement) const = 0;
    virtual std::string_view get_created_table_name(GWBUF* pStmt) const = 0;
    virtual DatabaseNames    get_database_names(GWBUF* pStmt) const = 0;
    virtual void             get_field_info(GWBUF* pStmt,
                                            const QC_FIELD_INFO** ppInfos,
                                            size_t* pnInfos) const = 0;
    virtual void             get_function_info(GWBUF* pStmt,
                                               const QC_FUNCTION_INFO** ppInfos,
                                               size_t* pnInfos) const = 0;
    virtual QC_KILL          get_kill_info(GWBUF* pStmt) const = 0;
    virtual qc_query_op_t    get_operation(GWBUF* pStmt) const = 0;
    virtual uint32_t         get_options() const = 0;
    virtual GWBUF*           get_preparable_stmt(GWBUF* pStmt) const = 0;
    virtual std::string_view get_prepare_name(GWBUF* pStmt) const = 0;
    virtual uint64_t         get_server_version() const = 0;
    virtual qc_sql_mode_t    get_sql_mode() const = 0;
    virtual TableNames       get_table_names(GWBUF* pStmt) const = 0;
    virtual uint32_t         get_trx_type_mask(GWBUF* pStmt) const = 0;
    uint32_t                 get_trx_type_mask_using(GWBUF* pStmt, qc_trx_parse_using_t use) const;
    virtual uint32_t         get_type_mask(GWBUF* pStmt) const = 0;
    virtual bool             is_drop_table_query(GWBUF* pStmt) const = 0;

    virtual bool set_options(uint32_t options) = 0;
    virtual void set_server_version(uint64_t version) = 0;
    virtual void set_sql_mode(qc_sql_mode_t sql_mode) = 0;
};

}
