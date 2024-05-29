/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxscale/parser.hh>
#include <maxscale/buffer.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>
#include "internal/modules.hh"

namespace
{

namespace sql = mxs::sql;

const char CN_ARGUMENTS[] = "arguments";
const char CN_CANONICAL[] = "canonical";
const char CN_CLASSIFY[] = "classify";
const char CN_FIELDS[] = "fields";
const char CN_FUNCTIONS[] = "functions";

static const sql::Type SQL_TYPES[] =
{
    /* Excluded by design */
    // sql::TYPE_UNKNOWN,
    sql::TYPE_READ,
    sql::TYPE_WRITE,
    sql::TYPE_MASTER_READ,
    sql::TYPE_SESSION_WRITE,
    sql::TYPE_USERVAR_WRITE,
    sql::TYPE_USERVAR_READ,
    sql::TYPE_SYSVAR_READ,
    /** Not implemented yet */
    // sql::TYPE_SYSVAR_WRITE,
    sql::TYPE_GSYSVAR_READ,
    sql::TYPE_GSYSVAR_WRITE,
    sql::TYPE_BEGIN_TRX,
    sql::TYPE_ENABLE_AUTOCOMMIT,
    sql::TYPE_DISABLE_AUTOCOMMIT,
    sql::TYPE_ROLLBACK,
    sql::TYPE_COMMIT,
    sql::TYPE_PREPARE_NAMED_STMT,
    sql::TYPE_PREPARE_STMT,
    sql::TYPE_EXEC_STMT,
    sql::TYPE_CREATE_TMP_TABLE,
    sql::TYPE_DEALLOC_PREPARE,
    sql::TYPE_READONLY,
    sql::TYPE_READWRITE,
    sql::TYPE_NEXT_TRX,
};

static const int N_SQL_TYPES = sizeof(SQL_TYPES) / sizeof(SQL_TYPES[0]);

struct type_name_info
{
    const char* name;
    size_t      name_len;
};

struct type_name_info type_to_type_name_info(sql::Type type)
{
    struct type_name_info info;

    switch (type)
    {
    case mxs::sql::TYPE_UNKNOWN:
        {
            static const char name[] = "sql::TYPE_UNKNOWN";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_READ:
        {
            static const char name[] = "sql::TYPE_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_WRITE:
        {
            static const char name[] = "sql::TYPE_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_MASTER_READ:
        {
            static const char name[] = "sql::TYPE_MASTER_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_SESSION_WRITE:
        {
            static const char name[] = "sql::TYPE_SESSION_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_USERVAR_WRITE:
        {
            static const char name[] = "sql::TYPE_USERVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_USERVAR_READ:
        {
            static const char name[] = "sql::TYPE_USERVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_SYSVAR_READ:
        {
            static const char name[] = "sql::TYPE_SYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    /** Not implemented yet */
    // case mxs::sql::TYPE_SYSVAR_WRITE:
    case mxs::sql::TYPE_GSYSVAR_READ:
        {
            static const char name[] = "sql::TYPE_GSYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_GSYSVAR_WRITE:
        {
            static const char name[] = "sql::TYPE_GSYSVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_BEGIN_TRX:
        {
            static const char name[] = "sql::TYPE_BEGIN_TRX";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_ENABLE_AUTOCOMMIT:
        {
            static const char name[] = "sql::TYPE_ENABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_DISABLE_AUTOCOMMIT:
        {
            static const char name[] = "sql::TYPE_DISABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_ROLLBACK:
        {
            static const char name[] = "sql::TYPE_ROLLBACK";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_COMMIT:
        {
            static const char name[] = "sql::TYPE_COMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_PREPARE_NAMED_STMT:
        {
            static const char name[] = "sql::TYPE_PREPARE_NAMED_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_PREPARE_STMT:
        {
            static const char name[] = "sql::TYPE_PREPARE_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_EXEC_STMT:
        {
            static const char name[] = "sql::TYPE_EXEC_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_CREATE_TMP_TABLE:
        {
            static const char name[] = "sql::TYPE_CREATE_TMP_TABLE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_DEALLOC_PREPARE:
        {
            static const char name[] = "sql::TYPE_DEALLOC_PREPARE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_READONLY:
        {
            static const char name[] = "sql::TYPE_READONLY";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_READWRITE:
        {
            static const char name[] = "sql::TYPE_READWRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case mxs::sql::TYPE_NEXT_TRX:
        {
            static const char name[] = "sql::TYPE_NEXT_TRX";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    default:
        {
            static const char name[] = "UNKNOWN_mxs::sql::TYPE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;
    }

    return info;
}

}

namespace maxscale
{

const char* parser::to_string(Parser::Result result)
{
    switch (result)
    {
    case Parser::Result::INVALID:
        return "Parser::Result::INVALID";

    case Parser::Result::TOKENIZED:
        return "Parser::Result::TOKENIZED";

    case Parser::Result::PARTIALLY_PARSED:
        return "Parser::Result::PARTIALLY_PARSED";

    case Parser::Result::PARSED:
        return "Parser::Result::PARSED";

    default:
        mxb_assert(!true);
        return "Unknown";
    }
}

const char* parser::to_string(Parser::KillType type)
{
    switch (type)
    {
    case Parser::KillType::CONNECTION:
        return "Parser::KillType::CONNECTION";

    case Parser::KillType::QUERY:
        return "Parser::KillType::QUERY";

    case Parser::KillType::QUERY_ID:
        return "Parser::KillType::QUERY_ID";

    default:
        mxb_assert(!true);
        return "Unknown";
    }
}

/**
 * Parser
 */

namespace
{

const char CN_FIELD_CONTEXT_FIELD_UNION[] = "Parser::FieldContext::FIELD_UNION";
const char CN_FIELD_CONTEXT_FIELD_SUBQUERY[] = "Parser::FieldContext::FIELD_SUBQUERY";

}

//static
const char* Parser::field_context_to_string(FieldContext context)
{
    switch (context)
    {
    case FIELD_UNION:
        return CN_FIELD_CONTEXT_FIELD_UNION;

    case FIELD_SUBQUERY:
        return CN_FIELD_CONTEXT_FIELD_SUBQUERY;

    default:
        mxb_assert(!true);
    }

    return nullptr;
}

//static
bool Parser::field_context_from_string(std::string_view s, FieldContext* pContext)
{
    bool rv = true;

    if (s == CN_FIELD_CONTEXT_FIELD_UNION)
    {
        *pContext = Parser::FieldContext::FIELD_UNION;
    }
    else if (s == CN_FIELD_CONTEXT_FIELD_SUBQUERY)
    {
        *pContext = Parser::FieldContext::FIELD_SUBQUERY;
    }
    else
    {
        MXB_WARNING("'%.*s' is not a valid field context.", (int)s.length(), s.data());
        rv = false;
    }

    return rv;
}

//static
json_t* Parser::field_context_to_json(uint32_t context)
{
    json_t* pRv = nullptr;

    if (context != 0)
    {
        if ((context & ~(FIELD_UNION | FIELD_SUBQUERY)) == 0)
        {
            pRv = json_array();

            if (context & FIELD_UNION)
            {
                json_array_append_new(pRv, json_string(field_context_to_string(FIELD_UNION)));
            }

            if (context & FIELD_SUBQUERY)
            {
                json_array_append_new(pRv, json_string(field_context_to_string(FIELD_SUBQUERY)));
            }
        }
        else
        {
            MXB_ERROR("%u is not a valid Parser::FieldInfo context.", context);
        }
    }

    return pRv;
}

//static
bool Parser::field_context_from_json(json_t* pArray, uint32_t* pContext)
{
    bool rv = true;
    uint32_t context = 0;

    size_t index;
    json_t* pValue;
    json_array_foreach(pArray, index, pValue)
    {
        const char* zValue = json_string_value(pValue);

        if (zValue)
        {
            FieldContext value;
            if (field_context_from_string(zValue, &value))
            {
                context |= value;
            }
            else
            {
                MXB_ERROR("'%s' is not a valid FieldContext.", zValue);
                rv = false;
            }
        }
        else
        {
            MXB_ERROR("Json array did not contain string values.");
            rv = false;
        }

        if (!rv)
        {
            break;
        }
    }

    if (rv)
    {
        *pContext = context;
    }

    return rv;
}

namespace
{

const char CN_RESULT_INVALID[] = "Parser::Result::INVALID";
const char CN_RESULT_TOKENIZED[] = "Parser::Result::TOKENIZED";
const char CN_RESULT_PARTIALLY_PARSED[] = "Parser::Result::PARTIALLY_PARSED";
const char CN_RESULT_PARSED[] = "Parser::Result::PARSED";
}

//static
const char* Parser::to_string(Result result)
{
    switch (result)
    {
    case Result::INVALID:
        return CN_RESULT_INVALID;

    case Result::TOKENIZED:
        return CN_RESULT_TOKENIZED;

    case Result::PARTIALLY_PARSED:
        return CN_RESULT_PARTIALLY_PARSED;

    case Result::PARSED:
        return CN_RESULT_PARSED;
    }

    mxb_assert(!true);
    return nullptr;
}

//static
bool Parser::from_string(std::string_view s, Result* pResult)
{
    bool rv = true;

    if (s == CN_RESULT_INVALID)
    {
        *pResult = Result::INVALID;
    }
    else if (s == CN_RESULT_TOKENIZED)
    {
        *pResult = Result::TOKENIZED;
    }
    else if (s == CN_RESULT_PARTIALLY_PARSED)
    {
        *pResult = Result::PARTIALLY_PARSED;
    }
    else if (s == CN_RESULT_PARSED)
    {
        *pResult = Result::PARTIALLY_PARSED;
    }
    else
    {
        rv = false;
    }

    return rv;
}

namespace
{

const char CN_KILL_TYPE_CONNECTION[] = "Parser::KillType::CONNECTION";
const char CN_KILL_TYPE_QUERY[] = "Parser::KillType::QUERY";
const char CN_KILL_TYPE_QUERY_ID[] = "Parser::KillType::QUERY_ID";

}

//static
const char* Parser::to_string(KillType kill_type)
{
    switch (kill_type)
    {
    case KillType::CONNECTION:
        return CN_KILL_TYPE_CONNECTION;

    case KillType::QUERY:
        return CN_KILL_TYPE_QUERY;

    case KillType::QUERY_ID:
        return CN_KILL_TYPE_QUERY_ID;
    }

    mxb_assert(!true);
    return nullptr;
}

//static
bool Parser::from_string(std::string_view s, KillType* pKill_type)
{
    bool rv = true;

    if (s == CN_KILL_TYPE_CONNECTION)
    {
        *pKill_type = KillType::CONNECTION;
    }
    else if (s == CN_KILL_TYPE_QUERY)
    {
        *pKill_type = KillType::QUERY;
    }
    else if (s == CN_KILL_TYPE_QUERY_ID)
    {
        *pKill_type = KillType::QUERY_ID;
    }
    else
    {
        MXB_WARNING("'%.*s' is not a valid kill type.", (int)s.length(), s.data());
        rv = false;
    }

    return rv;
}

namespace
{

const char CN_SQL_MODE_DEFAULT[] = "Parser::SqlMode::DEFAULT";
const char CN_SQL_MODE_ORACLE[] = "Parser::SqlMode::ORACLE";

}

//static
const char* Parser::to_string(SqlMode sql_mode)
{
    switch (sql_mode)
    {
    case SqlMode::DEFAULT:
        return CN_SQL_MODE_DEFAULT;

    case SqlMode::ORACLE:
        return CN_SQL_MODE_ORACLE;
    }

    mxb_assert(!true);
    return nullptr;
}

//static
bool Parser::from_string(std::string_view s, SqlMode* pSql_mode)
{
    bool rv = true;

    if (s == CN_SQL_MODE_DEFAULT)
    {
        *pSql_mode = SqlMode::DEFAULT;
    }
    else if (s == CN_SQL_MODE_ORACLE)
    {
        *pSql_mode = SqlMode::ORACLE;
    }
    else
    {
        MXB_WARNING("'%.*s' is not a valid sql mode.", (int)s.length(), s.data());
        rv = false;
    }

    return rv;
}

//static
json_t* Parser::to_json(const KillInfo& kill_info)
{
    json_t* pRv = json_object();

    json_object_set_new(pRv, "target", json_string(kill_info.target.c_str()));
    json_object_set_new(pRv, "user", json_boolean(kill_info.user));
    json_object_set_new(pRv, "soft", json_boolean(kill_info.soft));
    json_object_set_new(pRv, "type", json_string(to_string(kill_info.type)));

    return pRv;
}

//static
bool Parser::from_json(json_t* pObject, KillInfo* pKill_info)
{
    bool rv = false;

    json_t* pTarget = json_object_get(pObject, "target");
    json_t* pUser = json_object_get(pObject, "user");
    json_t* pSoft = json_object_get(pObject, "soft");
    json_t* pType = json_object_get(pObject, "type");

    if (pTarget && pUser && pSoft && pType)
    {
        KillInfo kill_info;

        if (json_is_string(pTarget))
        {
            kill_info.target = json_string_value(pTarget);

            if (json_is_boolean(pUser))
            {
                kill_info.user = json_boolean_value(pUser);

                if (json_is_boolean(pSoft))
                {
                    kill_info.soft = json_boolean_value(pSoft);
                }

                if (json_is_string(pType))
                {
                    KillType type;
                    if (from_string(json_string_value(pType), &type))
                    {
                        kill_info.type = type;

                        *pKill_info = std::move(kill_info);
                        rv = true;
                    }
                }
            }
        }
    }

    if (!rv)
    {
        MXB_WARNING("Json object does not seem to be a KillInfo.");
    }

    return rv;
}

//static
json_t* Parser::to_json(const TableNames& table_names)
{
    json_t* pRv = json_array();

    for (const TableName& table_name : table_names)
    {
        json_t* pTable_name = json_object();
        const char* p;
        size_t n;
        if (!table_name.db.empty())
        {
            p = table_name.db.data();
            n = table_name.db.length();

            json_object_set_new(pTable_name, "db", json_stringn(p, n));
        }

        p = table_name.table.data();
        n = table_name.table.length();

        json_object_set_new(pTable_name, "table", json_stringn(p, n));

        json_array_append_new(pRv, pTable_name);
    }

    return pRv;
}

//static
std::string Parser::type_mask_to_string(uint32_t type_mask)
{
    std::string rv;

    for (int i = 0; i < N_SQL_TYPES; ++i)
    {
        sql::Type type = SQL_TYPES[i];

        if (type_mask & type)
        {
            if (!rv.empty())
            {
                rv += "|";
            }

            struct type_name_info info = type_to_type_name_info(type);

            rv += info.name;
        }
    }

    return rv;
}

//static
uint32_t Parser::remove_non_trx_type_bits(uint32_t type_mask)
{
    if (Parser::type_mask_contains(type_mask, mxs::sql::TYPE_WRITE)
        && Parser::type_mask_contains(type_mask, mxs::sql::TYPE_COMMIT))
    {
        // This is a commit reported for "CREATE TABLE...",
        // "DROP TABLE...", etc. that cause an implicit commit.
        type_mask = 0;
    }
    else
    {
        // Only START TRANSACTION can be explicitly READ or WRITE.
        if (!(type_mask & mxs::sql::TYPE_BEGIN_TRX))
        {
            // So, strip them away for everything else.
            type_mask &= ~(mxs::sql::TYPE_WRITE | mxs::sql::TYPE_READ);
        }

        // Then leave only the bits related to transaction and
        // autocommit state.
        type_mask &= (mxs::sql::TYPE_BEGIN_TRX
                      | mxs::sql::TYPE_WRITE
                      | mxs::sql::TYPE_READ
                      | mxs::sql::TYPE_COMMIT
                      | mxs::sql::TYPE_ROLLBACK
                      | mxs::sql::TYPE_ENABLE_AUTOCOMMIT
                      | mxs::sql::TYPE_DISABLE_AUTOCOMMIT
                      | mxs::sql::TYPE_READONLY
                      | mxs::sql::TYPE_READWRITE
                      | mxs::sql::TYPE_NEXT_TRX);
    }

    return type_mask;
}

//static
const char* sql::to_string(sql::OpCode op)
{
    switch (op)
    {
#undef PP_SQL_OPCODE
#define PP_SQL_OPCODE(X) case X: return "sql::" #X ;
#include <maxscale/parser_opcode.hh>

    default:
        mxb_assert(!true);
        return "UNKNOWN_SQL_OP";
    }
}

namespace
{

void append_field_info(json_t* pParent,
                       const char* zName,
                       const Parser::FieldInfo* begin, const Parser::FieldInfo* end)
{
    json_t* pFields = json_array();

    std::for_each(begin, end, [pFields](const Parser::FieldInfo& info) {
                      std::string name;

                      if (!info.database.empty())
                      {
                          name += info.database;
                          name += '.';
                          mxb_assert(!info.table.empty());
                      }

                      if (!info.table.empty())
                      {
                          name += info.table;
                          name += '.';
                      }

                      mxb_assert(!info.column.empty());

                      name += info.column;

                      json_array_append_new(pFields, json_string(name.c_str()));
                  });

    json_object_set_new(pParent, zName, pFields);
}

void append_field_info(const mxs::Parser& parser, json_t* pParams, const GWBUF& stmt)
{
    const Parser::FieldInfo* begin;
    size_t n;
    parser.get_field_info(stmt, &begin, &n);

    append_field_info(pParams, CN_FIELDS, begin, begin + n);
}

void append_function_info(const mxs::Parser& parser, json_t* pParams, const GWBUF& stmt)
{
    json_t* pFunctions = json_array();

    const mxs::Parser::FunctionInfo* begin;
    size_t n;
    parser.get_function_info(stmt, &begin, &n);

    std::for_each(begin, begin + n, [pFunctions](const mxs::Parser::FunctionInfo& info) {
                      json_t* pFunction = json_object();

                      json_object_set_new(pFunction, CN_NAME,
                                          json_stringn(info.name.data(), info.name.length()));

                      append_field_info(pFunction, CN_ARGUMENTS, info.fields, info.fields + info.n_fields);

                      json_array_append_new(pFunctions, pFunction);
                  });

    json_object_set_new(pParams, CN_FUNCTIONS, pFunctions);
}
}

std::unique_ptr<json_t> Parser::parse_to_resource(const char* zHost, const GWBUF& stmt) const
{
    json_t* pAttributes = json_object();

    Parser::Result result = parse(stmt, mxs::Parser::COLLECT_ALL);

    json_object_set_new(pAttributes, CN_PARSE_RESULT, json_string(mxs::parser::to_string(result)));

    if (result != Parser::Result::INVALID)
    {
        std::string type_mask = mxs::Parser::type_mask_to_string(get_type_mask(stmt));
        json_object_set_new(pAttributes, CN_TYPE_MASK, json_string(type_mask.c_str()));

        json_object_set_new(pAttributes, CN_OPERATION,
                            json_string(mxs::sql::to_string(get_operation(stmt))));

        append_field_info(*this, pAttributes, stmt);
        append_function_info(*this, pAttributes, stmt);

        std::string canonical(get_canonical(stmt));
        json_object_set_new(pAttributes, CN_CANONICAL, json_string(canonical.c_str()));
    }

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_CLASSIFY, pSelf));
}

namespace
{

uint32_t get_trx_type_mask_using_default(const mxs::Parser& parser, const GWBUF& stmt)
{
    uint32_t type_mask = parser.get_type_mask(stmt);

    return Parser::remove_non_trx_type_bits(type_mask);
}

}

uint32_t Parser::get_trx_type_mask_using(const GWBUF& stmt, ParseTrxUsing use) const
{
    uint32_t type_mask = 0;

    switch (use)
    {
    case ParseTrxUsing::DEFAULT:
        type_mask = get_trx_type_mask_using_default(*this, stmt);
        break;

    case ParseTrxUsing::CUSTOM:
        type_mask = get_trx_type_mask(stmt);
        break;

    default:
        mxb_assert(!true);
    }

    return type_mask;
}


/**
 * ParserPlugin
 */

//static
ParserPlugin* ParserPlugin::load(const char* zPlugin_name)
{
    void* pModule_object = nullptr;
    auto pModule_info = get_module(zPlugin_name, mxs::ModuleType::PARSER);
    if (pModule_info)
    {
        pModule_object = pModule_info->module_object;
    }

    if (pModule_object)
    {
        MXB_INFO("%s loaded.", zPlugin_name);
    }
    else
    {
        MXB_ERROR("Could not load %s.", zPlugin_name);
    }

    return static_cast<ParserPlugin*>(pModule_object);
}

//static
void ParserPlugin::unload(ParserPlugin*)
{
    // TODO: The module loading/unloading needs an overhaul before we
    // TODO: actually can unload something.
}

}
