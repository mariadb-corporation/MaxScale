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

#define MXS_PARSER_VERSION {4, 0, 0}

class GWBUF;
struct json_t;

namespace maxscale
{

namespace sql
{

/**
 * Type defines bits that provide information about a particular statement.
 *
 * Note that more than one bit may be set for a single statement.
 */
enum Type
{
    TYPE_UNKNOWN            = 0,      /*< Initial value, can't be tested bitwisely */
    TYPE_LOCAL_READ         = 1 << 0, /*< Read non-database data, execute in MaxScale:any */
    TYPE_READ               = 1 << 1, /*< Read database data:any */
    TYPE_WRITE              = 1 << 2, /*< Master data will be  modified:master */
    TYPE_MASTER_READ        = 1 << 3, /*< Read from the master:master */
    TYPE_SESSION_WRITE      = 1 << 4, /*< Session data will be modified:master or all */
    TYPE_USERVAR_WRITE      = 1 << 5, /*< Write a user variable:master or all */
    TYPE_USERVAR_READ       = 1 << 6, /*< Read a user variable:master or any */
    TYPE_SYSVAR_READ        = 1 << 7, /*< Read a system variable:master or any */
    TYPE_GSYSVAR_READ       = 1 << 8, /*< Read global system variable:master or any */
    TYPE_GSYSVAR_WRITE      = 1 << 9, /*< Write global system variable:master or all */
    TYPE_BEGIN_TRX          = 1 << 10,/*< BEGIN or START TRANSACTION */
    TYPE_ENABLE_AUTOCOMMIT  = 1 << 11,/*< SET autocommit=1 */
    TYPE_DISABLE_AUTOCOMMIT = 1 << 12,/*< SET autocommit=0 */
    TYPE_ROLLBACK           = 1 << 13,/*< ROLLBACK */
    TYPE_COMMIT             = 1 << 14,/*< COMMIT */
    TYPE_PREPARE_NAMED_STMT = 1 << 15,/*< Prepared stmt with name from user:all */
    TYPE_PREPARE_STMT       = 1 << 16,/*< Prepared stmt with id provided by server:all */
    TYPE_EXEC_STMT          = 1 << 17,/*< Execute prepared statement:master or any */
    TYPE_CREATE_TMP_TABLE   = 1 << 18,/*< Create temporary table:master (could be all) */
    TYPE_READ_TMP_TABLE     = 1 << 19,/*< Read temporary table:master (could be any) */
    TYPE_SHOW_DATABASES     = 1 << 20,/*< Show list of databases */
    TYPE_SHOW_TABLES        = 1 << 21,/*< Show list of tables */
    TYPE_DEALLOC_PREPARE    = 1 << 22,/*< Dealloc named prepare stmt:all */
    TYPE_READONLY           = 1 << 23,/*< The READ ONLY part of SET TRANSACTION */
    TYPE_READWRITE          = 1 << 24,/*< The READ WRITE part of SET TRANSACTION  */
    TYPE_NEXT_TRX           = 1 << 25,/*< SET TRANSACTION that's only for the next transaction */
};

enum OpCode : uint32_t
{
    OP_UNDEFINED = 0,

    OP_ALTER,
    OP_ALTER_TABLE,
    OP_CALL,
    OP_CHANGE_DB,
    OP_CREATE,
    OP_CREATE_TABLE,
    OP_DELETE,
    OP_DROP,
    OP_DROP_TABLE,
    OP_EXECUTE,
    OP_EXPLAIN,
    OP_GRANT,
    OP_INSERT,
    OP_KILL,
    OP_LOAD,
    OP_LOAD_LOCAL,
    OP_REVOKE,
    OP_SELECT,
    OP_SET,
    OP_SET_TRANSACTION,
    OP_SHOW,
    OP_TRUNCATE,
    OP_UPDATE,
};

const char* to_string(OpCode code);

};

class ParserPlugin;

class Parser
{
public:
    enum class TypeMaskStatus
    {
        FINAL,
        NEEDS_PARSING
    };

    using PacketTypeMask = std::pair<uint32_t, TypeMaskStatus>;

    class Helper
    {
    public:

        virtual GWBUF            create_packet(std::string_view sql) const = 0;

        virtual const char*      client_command_to_string(uint32_t cmd) const = 0;
        virtual bool             command_will_respond(uint32_t cmd) const = 0;
        virtual bool             continues_ps(const GWBUF& packet, uint32_t prev_cmd) const = 0;
        virtual uint32_t         get_command(const GWBUF& packet) const = 0;
        virtual PacketTypeMask   get_packet_type_mask(const GWBUF& packet) const = 0;
        virtual uint32_t         get_ps_id(const GWBUF& packet) const = 0;
        virtual std::string_view get_sql(const GWBUF& packet) const = 0;
        virtual bool             is_empty(const GWBUF& packet) const = 0;
        virtual bool             is_execute_immediately_ps(uint32_t id) const = 0;
        virtual bool             is_multi_part_packet(const GWBUF& packet) const = 0;
        virtual bool             is_prepare(const GWBUF& packet) const = 0;
        virtual bool             is_ps_direct_exec_id(uint32_t id) const = 0;
        virtual bool             is_ps_packet(const GWBUF& packet) const = 0;
        virtual bool             is_query(const GWBUF& packet) const = 0;
    };

    struct TableName
    {
        TableName() = default;

        TableName(std::string_view table)
            : table(table)
        {
        }

        TableName(std::string_view db, std::string_view table)
            : db(db)
            , table(table)
        {
        }

        std::string_view db;
        std::string_view table;

        bool operator == (const TableName& rhs) const
        {
            return this->db == rhs.db && this->table == rhs.table;
        }

        bool operator < (const TableName& rhs) const
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

    using TableNames = std::vector<TableName>;
    using DatabaseNames = std::vector<std::string_view>;

    /**
     * Options to be used with set_options().
     */
    enum Option
    {
        OPTION_STRING_ARG_AS_FIELD = (1 << 0),   /*< Report a string argument to a function as a field. */
        OPTION_STRING_AS_FIELD     = (1 << 1),   /*< Report strings as fields. */
    };

    static constexpr uint32_t OPTION_MASK = OPTION_STRING_ARG_AS_FIELD | OPTION_STRING_AS_FIELD;

    /**
     * SqlMode specifies what should be assumed of the statements
     * that will be parsed.
     */
    enum class SqlMode
    {
        DEFAULT,    /*< Assume the statements are MariaDB SQL. */
        ORACLE      /*< Assume the statements are PL/SQL. */
    };


    enum class KillType
    {
        CONNECTION,
        QUERY,
        QUERY_ID
    };

    /**
     * Contains the information about a KILL command.
     */
    struct KillInfo
    {
        std::string target;                      // The string form target of the KILL
        bool        user = false;                // If true, the value in `target` is the name of a user.
        bool        soft = false;                // If true, the SOFT option was used
        KillType    type = KillType::CONNECTION; // Type of the KILL command
    };

    enum class ParseTrxUsing
    {
        DEFAULT, /**< Parse transaction state using default parser. */
        CUSTOM,  /**< Parse transaction state using limited custom parser.. */
    };

    /**
     * FieldContext defines the context where a field appears.
     *
     * NOTE: A particular bit does NOT mean that the field appears ONLY in the context,
     *       but it may appear in other contexts as well.
     */
    enum FieldContext
    {
        FIELD_UNION    = 1,  /** The field appears on the right hand side in a UNION. */
        FIELD_SUBQUERY = 2   /** The field appears in a subquery. */
    };

    /**
     * FieldInfo contains information about a field used in a statement.
     */
    struct FieldInfo
    {
        std::string_view database;      /** Present if the field is of the form "a.b.c", empty otherwise. */
        std::string_view table;         /** Present if the field is of the form "a.b", empty otherwise. */
        std::string_view column;        /** Always present. */
        uint32_t         context { 0 }; /** The context in which the field appears. */
    };

    /**
     * FunctionInfo contains information about a function used in a statement.
     */
    struct FunctionInfo
    {
        std::string_view name;               /** Name of function. */
        FieldInfo*       fields { nullptr }; /** What fields the function accesses. */
        uint32_t         n_fields { 0 };     /** The number of fields in @c fields. */
    };

    /**
     * Collect specifies what information should be collected during parsing.
     */
    enum Collect
    {
        COLLECT_ESSENTIALS = 0x00,   /*< Collect only the base minimum. */
        COLLECT_TABLES     = 0x01,   /*< Collect table names. */
        COLLECT_DATABASES  = 0x02,   /*< Collect database names. */
        COLLECT_FIELDS     = 0x04,   /*< Collect field information. */
        COLLECT_FUNCTIONS  = 0x08,   /*< Collect function information. */

        COLLECT_ALL = (COLLECT_TABLES | COLLECT_DATABASES | COLLECT_FIELDS | COLLECT_FUNCTIONS)
    };

    /**
     * Result defines the possible outcomes when a statement is parsed.
     */
    enum class Result
    {
        INVALID          = 0,  /*< The query was not recognized or could not be parsed. */
        TOKENIZED        = 1,  /*< The query was classified based on tokens; incompletely classified. */
        PARTIALLY_PARSED = 2,  /*< The query was only partially parsed; incompletely classified. */
        PARSED           = 3   /*< The query was fully parsed; completely classified. */
    };

    /**
     * StmtResult contains limited information about a particular statement.
     */
    struct StmtResult
    {
        Result      status { Result::INVALID };
        uint32_t    type_mask { 0 };
        sql::OpCode op { sql::OP_UNDEFINED };
    };

    virtual ~Parser() = default;

    static bool type_mask_contains(uint32_t type_mask, sql::Type type)
    {
        return (type_mask & (uint32_t)type) == (uint32_t)type;
    }

    static std::string type_mask_to_string(uint32_t type_mask);

    virtual ParserPlugin& plugin() const = 0;
    virtual const Helper& helper() const = 0;

    virtual Result           parse(const GWBUF& stmt, uint32_t collect) const = 0;
    std::unique_ptr<json_t>  parse_to_resource(const char* zHost, const GWBUF& stmt) const;

    virtual std::string_view get_canonical(const GWBUF& stmt) const = 0;

    // Shorthands for Handler functions.
    const char* client_command_to_string(uint32_t cmd) const
    {
        return helper().client_command_to_string(cmd);
    }

    bool command_will_respond(uint32_t cmd) const
    {
        return helper().command_will_respond(cmd);
    }

    bool continues_ps(const GWBUF& packet, uint32_t prev_cmd) const
    {
        return helper().continues_ps(packet, prev_cmd);
    }

    uint32_t get_command(const GWBUF& packet) const
    {
        return helper().get_command(packet);
    }

    PacketTypeMask get_packet_type_mask(const GWBUF& packet) const
    {
        return helper().get_packet_type_mask(packet);
    }

    uint32_t get_ps_id(const GWBUF& packet) const
    {
        return helper().get_ps_id(packet);
    }

    std::string_view get_sql(const GWBUF& stmt) const
    {
        return helper().get_sql(stmt);
    }

    bool is_empty(const GWBUF& packet) const
    {
        return helper().is_empty(packet);
    }

    bool is_execute_immediately_ps(uint32_t id) const
    {
        return helper().is_execute_immediately_ps(id);
    }

    bool is_multi_part_packet(const GWBUF& packet) const
    {
        return helper().is_multi_part_packet(packet);
    }

    bool is_prepare(const GWBUF& packet) const
    {
        return helper().is_prepare(packet);
    }

    bool is_ps_direct_exec_id(uint32_t id) const
    {
        return helper().is_ps_direct_exec_id(id);
    }

    bool is_ps_packet(const GWBUF& packet) const
    {
        return helper().is_ps_packet(packet);
    }

    bool is_query(const GWBUF& packet) const
    {
        return helper().is_query(packet);
    }


    virtual std::string_view get_created_table_name(const GWBUF& stmt) const = 0;
    virtual DatabaseNames    get_database_names(const GWBUF& stmt) const = 0;
    virtual void             get_field_info(const GWBUF& stmt,
                                            const FieldInfo** ppInfos,
                                            size_t* pnInfos) const = 0;
    virtual void             get_function_info(const GWBUF& stmt,
                                               const FunctionInfo** ppInfos,
                                               size_t* pnInfos) const = 0;
    virtual KillInfo         get_kill_info(const GWBUF& stmt) const = 0;
    virtual sql::OpCode      get_operation(const GWBUF& stmt) const = 0;
    virtual uint32_t         get_options() const = 0;
    virtual GWBUF*           get_preparable_stmt(const GWBUF& stmt) const = 0;
    virtual std::string_view get_prepare_name(const GWBUF& stmt) const = 0;
    virtual uint64_t         get_server_version() const = 0;
    virtual SqlMode          get_sql_mode() const = 0;
    virtual TableNames       get_table_names(const GWBUF& stmt) const = 0;
    virtual uint32_t         get_trx_type_mask(const GWBUF& stmt) const = 0;
    uint32_t                 get_trx_type_mask_using(const GWBUF& stmt, ParseTrxUsing use) const;
    virtual uint32_t         get_type_mask(const GWBUF& stmt) const = 0;

    virtual bool set_options(uint32_t options) = 0;
    virtual void set_server_version(uint64_t version) = 0;
    virtual void set_sql_mode(SqlMode sql_mode) = 0;
};

/**
 * ParserPlugin defines the object a parser plugin must
 * implement and return.
 */
class ParserPlugin
{
public:
    static ParserPlugin* load(const char* zPlugin_name);
    static void          unload(ParserPlugin* pPlugin);

    /**
     * Must be called once to setup the parser plugin.
     *
     * @param sql_mode  The default sql mode.
     * @param args      The value of `query_classifier_args` in the configuration file.
     *
     * @return True, if the parser plugin be setup, otherwise false.
     */
    virtual bool setup(Parser::SqlMode sql_mode, const char* args) = 0;

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
    virtual Parser::StmtResult get_stmt_result(const GWBUF::ProtocolInfo* info) = 0;

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
    virtual std::string_view get_canonical(const GWBUF::ProtocolInfo* info) = 0;

    /**
     * Create a parser.
     *
     * @param pHelper  The helper to be used when needed.
     *                 Must exist for as long as the parser does.
     *
     * @return A new parser.
     */
    virtual std::unique_ptr<Parser> create_parser(const Parser::Helper* pHelper) const = 0;
};



namespace parser
{

const char* to_string(Parser::Result result);
const char* to_string(Parser::KillType type);

}

inline std::ostream& operator << (std::ostream& out, const Parser::TableName& x)
{
    out << x.to_string();
    return out;
}
}
