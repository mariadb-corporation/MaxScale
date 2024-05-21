/*
 * Copyright (c) 2025 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxscale/parser.hh>
#include <set>
#include <string>
#include <vector>

using mxs::Parser;
using std::cerr;
using std::endl;
using std::set;
using std::string_view;
using std::string;
using std::vector;

mxs::ParserPlugin* load_plugin(const char* zName)
{
    bool loaded = false;
    size_t len = strlen(zName);
    char libdir[len + 3 + 1];   // Extra for ../

    sprintf(libdir, "../%s", zName);

    mxs::set_libdir(libdir);

    mxs::ParserPlugin* pPlugin = mxs::ParserPlugin::load(zName);

    if (!pPlugin)
    {
        cerr << "error: Could not load classifier " << zName << "." << endl;
    }

    return pPlugin;
}

mxs::ParserPlugin* get_plugin(const char* zName, mxs::Parser::SqlMode sql_mode, const char* zArgs)
{
    mxs::ParserPlugin* pPlugin = nullptr;

    if (zName)
    {
        pPlugin = load_plugin(zName);

        if (pPlugin)
        {
            setenv("PP_ARGS", zArgs, 1);

            if (!pPlugin->setup(sql_mode) || !pPlugin->thread_init())
            {
                cerr << "error: Could not setup or init classifier " << zName << "." << endl;
                mxs::ParserPlugin::unload(pPlugin);
                pPlugin = 0;
            }
        }
    }

    return pPlugin;
}

void put_plugin(mxs::ParserPlugin* pPlugin)
{
    if (pPlugin)
    {
        pPlugin->thread_end();
        mxs::ParserPlugin::unload(pPlugin);
    }
}


class ParserUtil
{
public:
    enum class Verbosity
    {
        MIN,
        NORMAL,
        MAX
    };

    ParserUtil(mxs::Parser* pParser, mxs::Parser::SqlMode sql_mode, Verbosity verbosity)
        : m_parser(*pParser)
        , m_sql_mode(sql_mode)
        , m_verbosity(verbosity)
    {
    }

protected:
    std::string prefix(const char* zMessage = "error") const
    {
        std::stringstream ss;
        ss << zMessage << ": (" << m_file << ", " << m_line << "): ";
        return ss.str();
    }

    json_t* get_classification(const GWBUF& packet) const
    {
        json_t* pClassification = json_object();
        set_database_names(pClassification, packet);
        set_field_info(pClassification, packet);
        set_function_info(pClassification, packet);
        set_kill_info(pClassification, packet);
        set_operation(pClassification, packet);
        set_preparable_stmt(pClassification, packet);
        set_prepare_name(pClassification, packet);
        set_table_names(pClassification, packet);
        set_trx_type_mask(pClassification, packet);
        set_type_mask(pClassification, packet);
        set_relates_to_previous(pClassification, packet);
        set_is_multi_stmt(pClassification, packet);

        return pClassification;
    }

    int check_classification(const GWBUF& stmt, json_t* pClassification) const
    {
        int errors = 0;
        errors += !check_database_names(stmt, pClassification);
        errors += !check_field_info(stmt, pClassification);
        errors += !check_function_info(stmt, pClassification);
        errors += !check_kill_info(stmt, pClassification);
        errors += !check_operation(stmt, pClassification);
        errors += !check_preparable_stmt(stmt, pClassification);
        errors += !check_prepare_name(stmt, pClassification);
        errors += !check_table_names(stmt, pClassification);
        errors += !check_trx_type_mask(stmt, pClassification);
        errors += !check_type_mask(stmt, pClassification);
        errors += !check_relates_to_previous(stmt, pClassification);
        errors += !check_is_multi_stmt(stmt, pClassification);

        return errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /*
     * database_names
     */
    json_t* get_database_names(const GWBUF& packet) const
    {
        json_t* pDatabase_names = nullptr;

        vector<string_view> names = m_parser.get_database_names(packet);

        if (!names.empty())
        {
            pDatabase_names = json_array();
            for (auto name : names)
            {
                json_array_append_new(pDatabase_names, json_stringn(name.data(), name.length()));
            }
        }

        return pDatabase_names;
    }

    void set_database_names(json_t* pC, const GWBUF& packet) const
    {
        json_t* pDatabase_names = get_database_names(packet);

        if (pDatabase_names)
        {
            json_object_set_new(pC, "database_names", pDatabase_names);
        }
    }

    bool check_database_names(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "database_names");
        json_t* pGot = get_database_names(packet);

        return compare("database_names", pExpected, pGot);
    }

    /*
     * field_info
     */
    json_t* get_field_info(const GWBUF& packet) const
    {
        json_t* pField_info = nullptr;

        const Parser::FieldInfo* pInfos;
        size_t nInfos;
        m_parser.get_field_info(packet, &pInfos, &nInfos);

        if (nInfos != 0)
        {
            pField_info = json_array();

            for (size_t i = 0; i < nInfos; ++i)
            {
                const Parser::FieldInfo& info = pInfos[i];

                json_array_append_new(pField_info, to_json(info));
            }
        }

        return pField_info;
    }

    void set_field_info(json_t* pC, const GWBUF& packet) const
    {
        json_t* pField_info = get_field_info(packet);

        if (pField_info)
        {
            json_object_set_new(pC, "field_info", pField_info);
        }
    }

    bool check_field_info(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "field_info");
        json_t* pGot = get_field_info(packet);

        return compare("field_info", pExpected, pGot);
    }

    /*
     * function_info
     */
    json_t* get_function_info(const GWBUF& packet) const
    {
        json_t* pFunction_info = nullptr;

        const Parser::FunctionInfo* pInfos;
        size_t nInfos;
        m_parser.get_function_info(packet, &pInfos, &nInfos);

        if (nInfos != 0)
        {
            pFunction_info = json_array();

            for (size_t i = 0; i < nInfos; ++i)
            {
                const Parser::FunctionInfo& info = pInfos[i];

                json_t* pInfo = json_object();
                json_object_set_new(pInfo, "name", to_json(info.name));

                json_t* pFields = json_array();
                for (uint32_t j = 0; j < info.n_fields; ++j)
                {
                    json_array_append_new(pFields, to_json(info.fields[j]));
                }

                json_object_set_new(pInfo, "fields", pFields);

                json_array_append_new(pFunction_info, pInfo);
            }
        }

        return pFunction_info;

    }
    void set_function_info(json_t* pC, const GWBUF& packet) const
    {
        json_t* pFunction_info = get_function_info(packet);

        if (pFunction_info)
        {
            json_object_set_new(pC, "function_info", pFunction_info);
        }
    }

    bool check_function_info(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "function_info");
        json_t* pGot = get_function_info(packet);

        return compare("function_info", pExpected, pGot);
    }

    /*
     * kill_info
     */
    json_t* get_kill_info(const GWBUF& packet) const
    {
        json_t* pKill_info = nullptr;

        if (m_parser.get_operation(packet) == mxs::sql::OpCode::OP_KILL)
        {
            Parser::KillInfo kill_info = m_parser.get_kill_info(packet);

            pKill_info = Parser::to_json(kill_info);
        }

        return pKill_info;
    }

    void set_kill_info(json_t* pC, const GWBUF& packet) const
    {
        json_t* pKill_info = get_kill_info(packet);

        if (pKill_info)
        {
            json_object_set_new(pC, "kill_info", pKill_info);
        }
    }

    bool check_kill_info(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "kill_info");
        json_t* pGot = get_kill_info(packet);

        return compare("kill_info", pExpected, pGot);
    }

    /*
     * operations
     */
    json_t* get_operation(const GWBUF& packet) const
    {
        return json_string(mxs::sql::to_string(m_parser.get_operation(packet)));
    }

    void set_operation(json_t* pC, const GWBUF& packet) const
    {
        json_object_set_new(pC, "operation", get_operation(packet));
    }

    bool check_operation(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "operation");
        json_t* pGot = get_operation(packet);

        return compare("operation", pExpected, pGot);
    }

    /*
     * preparable_stmt
     */
    json_t* get_preparable_stmt(const GWBUF& packet) const
    {
        json_t* pPreparable_stmt = nullptr;

        GWBUF* pStmt = m_parser.get_preparable_stmt(packet);

        if (pStmt)
        {
            pPreparable_stmt = get_classification(*pStmt);
        }

        return pPreparable_stmt;
    }

    void set_preparable_stmt(json_t* pC, const GWBUF& packet) const
    {
        json_t* pPreparable_stmt = get_preparable_stmt(packet);

        if (pPreparable_stmt)
        {
            json_object_set_new(pC, "preparable_stmt", pPreparable_stmt);
        }
    }

    bool check_preparable_stmt(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "preparable_stmt");
        json_t* pGot = get_preparable_stmt(packet);

        return compare("preparable_stmt", pExpected, pGot);
    }

    /*
     * prepare_name
     */
    json_t* get_prepare_name(const GWBUF& packet) const
    {
        json_t* pPrepare_name = nullptr;

        string_view s = m_parser.get_prepare_name(packet);

        if (!s.empty())
        {
            pPrepare_name = json_stringn(s.data(), s.length());
        }

        return pPrepare_name;
    }

    void set_prepare_name(json_t* pC, const GWBUF& packet) const
    {
        json_t* pPrepare_name = get_prepare_name(packet);

        if (pPrepare_name)
        {
            json_object_set_new(pC, "prepare_name", pPrepare_name);
        }
    }

    bool check_prepare_name(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "prepare_name");
        json_t* pGot = get_prepare_name(packet);

        return compare("prepare_name", pExpected, pGot);
    }

    /*
     * table_names
     */
    json_t* get_table_names(const GWBUF& packet) const
    {
        json_t* pTable_names = nullptr;

        Parser::TableNames table_names = m_parser.get_table_names(packet);

        if (!table_names.empty())
        {
            pTable_names = Parser::to_json(table_names);
        }

        return pTable_names;
    }

    void set_table_names(json_t* pC, const GWBUF& packet) const
    {
        json_t* pTable_names = get_table_names(packet);

        if (pTable_names)
        {
            json_object_set_new(pC, "table_names", pTable_names);
        }
    }

    bool check_table_names(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "table_names");
        json_t* pGot = get_table_names(packet);

        return compare("table_names", pExpected, pGot);
    }

    /*
     * trx_type_mask
     */
    json_t* get_trx_type_mask(const GWBUF& packet) const
    {
        json_t* pTrx_type_mask = nullptr;

        uint32_t trx_type_mask = m_parser.get_trx_type_mask(packet);

        if (trx_type_mask != 0)
        {
            string s = Parser::type_mask_to_string(trx_type_mask);
            pTrx_type_mask = json_string(s.c_str());
        }

        return pTrx_type_mask;
    }

    void set_trx_type_mask(json_t* pC, const GWBUF& packet) const
    {
        json_t* pTrx_type_mask = get_trx_type_mask(packet);

        if (pTrx_type_mask)
        {
            json_object_set_new(pC, "trx_type_mask", pTrx_type_mask);
        }
    }

    bool check_trx_type_mask(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "trx_type_mask");
        json_t* pGot = get_trx_type_mask(packet);

        return compare("trx_type_mask", pExpected, pGot);
    }

    /*
     * type_mask
     */
    json_t* get_type_mask(const GWBUF& packet) const
    {
        uint32_t type_mask = m_parser.get_type_mask(packet);
        string s = Parser::type_mask_to_string(type_mask);

        return json_string(s.c_str());
    }

    void set_type_mask(json_t* pC, const GWBUF& packet) const
    {
        json_t* pType_mask = get_type_mask(packet);

        json_object_set_new(pC, "type_mask", pType_mask);
    }

    bool check_type_mask(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "type_mask");
        json_t* pGot = get_type_mask(packet);

        return compare("type_mask", pExpected, pGot);
    }

    /*
     * relates_to_previous
     */
    json_t* get_relates_to_previous(const GWBUF& packet) const
    {
        json_t* pRelates_to_previous = nullptr;

        bool relates_to_previous = m_parser.relates_to_previous(packet);

        if (relates_to_previous)
        {
            pRelates_to_previous = json_boolean(true);
        }

        return pRelates_to_previous;
    }

    void set_relates_to_previous(json_t* pC, const GWBUF& packet) const
    {
        json_t* pRelates_to_previous = get_relates_to_previous(packet);

        if (pRelates_to_previous)
        {
            json_object_set_new(pC, "relates_to_previous", pRelates_to_previous);
        }
    }

    bool check_relates_to_previous(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "relates_to_previous");
        json_t* pGot = get_relates_to_previous(packet);

        return compare("relates_to_previous", pExpected, pGot);
    }

    /*
     * is_multi_stmt
     */
    json_t* get_is_multi_stmt(const GWBUF& packet) const
    {
        json_t* pIs_multi_stmt = nullptr;

        bool is_multi_stmt = m_parser.is_multi_stmt(packet);

        if (is_multi_stmt)
        {
            pIs_multi_stmt = json_boolean(true);
        }

        return pIs_multi_stmt;
    }

    void set_is_multi_stmt(json_t* pC, const GWBUF& packet) const
    {
        json_t* pIs_multi_stmt = get_is_multi_stmt(packet);

        if (pIs_multi_stmt)
        {
            json_object_set_new(pC, "is_multi_stmt", pIs_multi_stmt);
        }
    }

    bool check_is_multi_stmt(const GWBUF& packet, json_t* pC) const
    {
        json_t* pExpected = json_object_get(pC, "is_multi_stmt");
        json_t* pGot = get_is_multi_stmt(packet);

        return compare("is_multi_stmt", pExpected, pGot);
    }

    bool compare(const char* zWhat, json_t* pExpected, json_t* pGot) const
    {
        bool rv = false;

        if (pExpected && pGot)
        {
            rv = json_equal(pExpected, pGot);
        }
        else if (!pExpected && !pGot)
        {
            rv = true;
        }
        else
        {
            rv = false;
        }

        if (!rv)
        {
            cerr << "error (" << m_file << ", " << m_line << ", '" << zWhat << "'): "
                 << "expected ";

            if (pExpected)
            {
                cerr << "'" << mxb::json_dump(pExpected, 0) << "', ";
            }
            else
            {
                cerr << "nothing, ";
            }

            cerr << "got ";

            if (pGot)
            {
                cerr << "'" << mxb::json_dump(pGot, 0) << "'.";
            }
            else
            {
                cerr << "nothing.";
            }

            cerr << endl;

            rv = false;
        }

        if (pGot)
        {
            json_decref(pGot);
        }

        return rv;
    }

    static json_t* to_json(const Parser::FieldInfo& info)
    {
        json_t* pField_info = json_object();
        if (!info.database.empty())
        {
            json_object_set_new(pField_info, "database", to_json(info.database));
        }

        if (!info.table.empty())
        {
            json_object_set_new(pField_info, "table", to_json(info.table));
        }

        json_object_set_new(pField_info, "column", to_json(info.column));

        if (info.context != 0)
        {
            json_object_set_new(pField_info, "context", Parser::field_context_to_json(info.context));
        }

        return pField_info;
    }

    static json_t* to_json(string_view s)
    {
        return json_stringn(s.data(), s.length());
    }

    static std::string to_string(json_t* pJson)
    {
        string rv;

        if (pJson)
        {
            char* zJson = json_dumps(pJson, 0);
            rv = zJson;
            free(zJson);
        }
        else
        {
            rv = "nothing";
        }

        return rv;
    }

    mxs::Parser&               m_parser;
    const mxs::Parser::SqlMode m_sql_mode;
    const Verbosity            m_verbosity;
    std::string                m_file;
    int                        m_line {0};
};
