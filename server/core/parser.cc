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
#include <maxscale/parser.hh>
#include <maxscale/buffer.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>
#include "internal/modules.hh"

namespace
{

const char CN_ARGUMENTS[] = "arguments";
const char CN_CANONICAL[] = "canonical";
const char CN_CLASSIFY[] = "classify";
const char CN_FIELDS[] = "fields";
const char CN_FUNCTIONS[] = "functions";

static const qc_query_type_t QUERY_TYPES[] =
{
    /* Excluded by design */
    // QUERY_TYPE_UNKNOWN,
    QUERY_TYPE_LOCAL_READ,
    QUERY_TYPE_READ,
    QUERY_TYPE_WRITE,
    QUERY_TYPE_MASTER_READ,
    QUERY_TYPE_SESSION_WRITE,
    QUERY_TYPE_USERVAR_WRITE,
    QUERY_TYPE_USERVAR_READ,
    QUERY_TYPE_SYSVAR_READ,
    /** Not implemented yet */
    // QUERY_TYPE_SYSVAR_WRITE,
    QUERY_TYPE_GSYSVAR_READ,
    QUERY_TYPE_GSYSVAR_WRITE,
    QUERY_TYPE_BEGIN_TRX,
    QUERY_TYPE_ENABLE_AUTOCOMMIT,
    QUERY_TYPE_DISABLE_AUTOCOMMIT,
    QUERY_TYPE_ROLLBACK,
    QUERY_TYPE_COMMIT,
    QUERY_TYPE_PREPARE_NAMED_STMT,
    QUERY_TYPE_PREPARE_STMT,
    QUERY_TYPE_EXEC_STMT,
    QUERY_TYPE_CREATE_TMP_TABLE,
    QUERY_TYPE_READ_TMP_TABLE,
    QUERY_TYPE_SHOW_DATABASES,
    QUERY_TYPE_SHOW_TABLES,
    QUERY_TYPE_DEALLOC_PREPARE,
    QUERY_TYPE_READONLY,
    QUERY_TYPE_READWRITE,
    QUERY_TYPE_NEXT_TRX,
};

static const int N_QUERY_TYPES = sizeof(QUERY_TYPES) / sizeof(QUERY_TYPES[0]);
static const int QUERY_TYPE_MAX_LEN = 29;   // strlen("QUERY_TYPE_PREPARE_NAMED_STMT");

struct type_name_info
{
    const char* name;
    size_t      name_len;
};

struct type_name_info type_to_type_name_info(qc_query_type_t type)
{
    struct type_name_info info;

    switch (type)
    {
    case QUERY_TYPE_UNKNOWN:
        {
            static const char name[] = "QUERY_TYPE_UNKNOWN";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_LOCAL_READ:
        {
            static const char name[] = "QUERY_TYPE_LOCAL_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READ:
        {
            static const char name[] = "QUERY_TYPE_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_WRITE:
        {
            static const char name[] = "QUERY_TYPE_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_MASTER_READ:
        {
            static const char name[] = "QUERY_TYPE_MASTER_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SESSION_WRITE:
        {
            static const char name[] = "QUERY_TYPE_SESSION_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_USERVAR_WRITE:
        {
            static const char name[] = "QUERY_TYPE_USERVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_USERVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_USERVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SYSVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_SYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    /** Not implemented yet */
    // case QUERY_TYPE_SYSVAR_WRITE:
    case QUERY_TYPE_GSYSVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_GSYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_GSYSVAR_WRITE:
        {
            static const char name[] = "QUERY_TYPE_GSYSVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_BEGIN_TRX:
        {
            static const char name[] = "QUERY_TYPE_BEGIN_TRX";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_ENABLE_AUTOCOMMIT:
        {
            static const char name[] = "QUERY_TYPE_ENABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_DISABLE_AUTOCOMMIT:
        {
            static const char name[] = "QUERY_TYPE_DISABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_ROLLBACK:
        {
            static const char name[] = "QUERY_TYPE_ROLLBACK";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_COMMIT:
        {
            static const char name[] = "QUERY_TYPE_COMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_PREPARE_NAMED_STMT:
        {
            static const char name[] = "QUERY_TYPE_PREPARE_NAMED_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_PREPARE_STMT:
        {
            static const char name[] = "QUERY_TYPE_PREPARE_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_EXEC_STMT:
        {
            static const char name[] = "QUERY_TYPE_EXEC_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_CREATE_TMP_TABLE:
        {
            static const char name[] = "QUERY_TYPE_CREATE_TMP_TABLE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READ_TMP_TABLE:
        {
            static const char name[] = "QUERY_TYPE_READ_TMP_TABLE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SHOW_DATABASES:
        {
            static const char name[] = "QUERY_TYPE_SHOW_DATABASES";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SHOW_TABLES:
        {
            static const char name[] = "QUERY_TYPE_SHOW_TABLES";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_DEALLOC_PREPARE:
        {
            static const char name[] = "QUERY_TYPE_DEALLOC_PREPARE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READONLY:
        {
            static const char name[] = "QUERY_TYPE_READONLY";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READWRITE:
        {
            static const char name[] = "QUERY_TYPE_READWRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_NEXT_TRX:
        {
            static const char name[] = "QUERY_TYPE_NEXT_TRX";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    default:
        {
            static const char name[] = "UNKNOWN_QUERY_TYPE";
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

//static
std::string Parser::type_mask_to_string(uint32_t type_mask)
{
    std::string rv;

    for (int i = 0; i < N_QUERY_TYPES; ++i)
    {
        qc_query_type_t type = QUERY_TYPES[i];

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
const char* sql::to_string(sql::OpCode op)
{
    switch (op)
    {
    case OP_UNDEFINED:
        return "sql::OP_UNDEFINED";

    case OP_ALTER:
        return "sql::OP_ALTER";

    case OP_CALL:
        return "sql::OP_CALL";

    case OP_CHANGE_DB:
        return "sql::OP_CHANGE_DB";

    case OP_CREATE:
        return "sql::OP_CREATE";

    case OP_DELETE:
        return "sql::OP_DELETE";

    case OP_DROP:
        return "sql::OP_DROP";

    case OP_EXPLAIN:
        return "sql::OP_EXPLAIN";

    case OP_GRANT:
        return "sql::OP_GRANT";

    case OP_INSERT:
        return "sql::OP_INSERT";

    case OP_LOAD:
        return "sql::OP_LOAD";

    case OP_LOAD_LOCAL:
        return "sql::OP_LOAD_LOCAL";

    case OP_REVOKE:
        return "sql::OP_REVOKE";

    case OP_SELECT:
        return "sql::OP_SELECT";

    case OP_SET:
        return "sql::OP_SET";

    case OP_SET_TRANSACTION:
        return "sql::OP_SET_TRANSACTION";

    case OP_SHOW:
        return "sql::OP_SHOW";

    case OP_TRUNCATE:
        return "sql::OP_TRUNCATE";

    case OP_UPDATE:
        return "sql::OP_UPDATE";

    case OP_KILL:
        return "sql::OP_KILL";

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

void append_field_info(const mxs::Parser& parser, json_t* pParams, GWBUF& stmt)
{
    const Parser::FieldInfo* begin;
    size_t n;
    parser.get_field_info(stmt, &begin, &n);

    append_field_info(pParams, CN_FIELDS, begin, begin + n);
}

void append_function_info(const mxs::Parser& parser, json_t* pParams, GWBUF& stmt)
{
    json_t* pFunctions = json_array();

    const mxs::Parser::FunctionInfo* begin;
    size_t n;
    parser.get_function_info(stmt, &begin, &n);

    std::for_each(begin, begin + n, [&parser, pFunctions](const mxs::Parser::FunctionInfo& info) {
                      json_t* pFunction = json_object();

                      json_object_set_new(pFunction, CN_NAME,
                                          json_stringn(info.name.data(), info.name.length()));

                      append_field_info(pFunction, CN_ARGUMENTS, info.fields, info.fields + info.n_fields);

                      json_array_append_new(pFunctions, pFunction);
                  });

    json_object_set_new(pParams, CN_FUNCTIONS, pFunctions);
}
}

std::unique_ptr<json_t> Parser::parse_to_resource(const char* zHost, const std::string& statement) const
{
    json_t* pAttributes = json_object();

    GWBUF stmt = create_buffer(statement);

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

        json_object_set_new(pAttributes, CN_CANONICAL, json_string(stmt.get_canonical().c_str()));
    }

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_CLASSIFY, pSelf));
}

namespace
{

uint32_t get_trx_type_mask_using_default(const mxs::Parser& parser, GWBUF& stmt)
{
    uint32_t type_mask = parser.get_type_mask(stmt);

    if (Parser::type_mask_contains(type_mask, QUERY_TYPE_WRITE)
        && Parser::type_mask_contains(type_mask, QUERY_TYPE_COMMIT))
    {
        // This is a commit reported for "CREATE TABLE...",
        // "DROP TABLE...", etc. that cause an implicit commit.
        type_mask = 0;
    }
    else
    {
        // Only START TRANSACTION can be explicitly READ or WRITE.
        if (!(type_mask & QUERY_TYPE_BEGIN_TRX))
        {
            // So, strip them away for everything else.
            type_mask &= ~(QUERY_TYPE_WRITE | QUERY_TYPE_READ);
        }

        // Then leave only the bits related to transaction and
        // autocommit state.
        type_mask &= (QUERY_TYPE_BEGIN_TRX
                      | QUERY_TYPE_WRITE
                      | QUERY_TYPE_READ
                      | QUERY_TYPE_COMMIT
                      | QUERY_TYPE_ROLLBACK
                      | QUERY_TYPE_ENABLE_AUTOCOMMIT
                      | QUERY_TYPE_DISABLE_AUTOCOMMIT
                      | QUERY_TYPE_READONLY
                      | QUERY_TYPE_READWRITE
                      | QUERY_TYPE_NEXT_TRX);
    }

    return type_mask;
}

}

uint32_t Parser::get_trx_type_mask_using(GWBUF& stmt, ParseTrxUsing use) const
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
