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

using mxs::Parser;
using std::cerr;
using std::endl;
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
    ParserUtil(mxs::Parser* pParser)
        : m_parser(*pParser)
    {
    }

protected:
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
        set_type_mask(pClassification, packet);
        set_relates_to_previous(pClassification, packet);
        set_is_multi_stmt(pClassification, packet);

        return pClassification;
    }

    void set_database_names(json_t* pC, const GWBUF& packet) const
    {
        vector<string_view> names = m_parser.get_database_names(packet);

        if (!names.empty())
        {
            json_t* pNames = json_array();
            for (auto name : names)
            {
                json_array_append_new(pNames, json_stringn(name.data(), name.length()));
            }

            json_object_set_new(pC, "database_names", pNames);
        }
    }

    void set_field_info(json_t* pC, const GWBUF& packet) const
    {
        const Parser::FieldInfo* pInfos;
        size_t nInfos;
        m_parser.get_field_info(packet, &pInfos, &nInfos);

        if (nInfos != 0)
        {
            json_t* pField_infos = json_array();

            for (size_t i = 0; i < nInfos; ++i)
            {
                const Parser::FieldInfo& info = pInfos[i];

                json_array_append_new(pField_infos, to_json(info));
            }

            json_object_set_new(pC, "field_infos", pField_infos);
        }
    }

    void set_function_info(json_t* pC, const GWBUF& packet) const
    {
        const Parser::FunctionInfo* pInfos;
        size_t nInfos;
        m_parser.get_function_info(packet, &pInfos, &nInfos);

        if (nInfos != 0)
        {
            json_t* pFunction_infos = json_array();

            for (size_t i = 0; i < nInfos; ++i)
            {
                const Parser::FunctionInfo& info = pInfos[i];

                json_t* pFunction_info = json_object();
                json_object_set_new(pFunction_info, "name", to_json(info.name));

                json_t* pFields = json_array();
                for (uint32_t j = 0; j < info.n_fields; ++j)
                {
                    json_array_append_new(pFields, to_json(info.fields[j]));
                }

                json_object_set_new(pFunction_info, "fields", pFields);

                json_array_append_new(pFunction_infos, pFunction_info);
            }

            json_object_set_new(pC, "function_infos", pFunction_infos);
        }
    }

    void set_kill_info(json_t* pC, const GWBUF& packet) const
    {
        if (m_parser.get_operation(packet) == mxs::sql::OpCode::OP_KILL)
        {
            Parser::KillInfo kill_info = m_parser.get_kill_info(packet);

            json_object_set_new(pC, "kill_info", Parser::to_json(kill_info));
        }
    }

    void set_operation(json_t* pC, const GWBUF& packet) const
    {
        auto op = m_parser.get_operation(packet);
        json_object_set_new(pC, "operation", json_string(mxs::sql::to_string(op)));
    }

    void set_preparable_stmt(json_t* pC, const GWBUF& packet) const
    {
        GWBUF* pStmt = m_parser.get_preparable_stmt(packet);

        if (pStmt)
        {
            json_object_set_new(pC, "preparable_stmt", get_classification(*pStmt));
        }
    }

    void set_prepare_name(json_t* pC, const GWBUF& packet) const
    {
        string_view s = m_parser.get_prepare_name(packet);

        if (!s.empty())
        {
            json_object_set_new(pC, "prepare_name", json_stringn(s.data(), s.length()));
        }
    }

    void set_table_names(json_t* pC, const GWBUF& packet) const
    {
        Parser::TableNames table_names = m_parser.get_table_names(packet);

        if (!table_names.empty())
        {
            json_object_set_new(pC, "table_names", Parser::to_json(table_names));
        }
    }

    void set_type_mask(json_t* pC, const GWBUF& packet) const
    {
        uint32_t type_mask = m_parser.get_type_mask(packet);
        string s = Parser::type_mask_to_string(type_mask);

        json_object_set_new(pC, "type_mask", json_string(s.c_str()));
    }

    void set_relates_to_previous(json_t* pC, const GWBUF& packet) const
    {
        bool relates_to_previous = m_parser.relates_to_previous(packet);

        if (relates_to_previous)
        {
            json_object_set_new(pC, "relates_to_previous", json_boolean(true));
        }
    }

    void set_is_multi_stmt(json_t* pC, const GWBUF& packet) const
    {
        bool is_multi_stmt = m_parser.is_multi_stmt(packet);

        if (is_multi_stmt)
        {
            json_object_set_new(pC, "is_multi_stmt", json_boolean(true));
        }
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

    mxs::Parser& m_parser;
};
