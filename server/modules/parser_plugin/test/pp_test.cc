/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <fstream>
#include <iostream>
#include <maxscale/paths.hh>
#include "testreader.hh"
#include "utils.hh"

using namespace std;

using mxs::Parser;
using mxs::ParserPlugin;

namespace
{

void print_usage_and_exit(const char* zName)
{

    cerr << "usage: " << zName << " file [file*]" << endl;
    exit(EXIT_FAILURE);
}

}

namespace
{

class Tester : public ParserUtil
{
public:
    Tester(Parser* pParser)
        : ParserUtil(pParser)
    {
    }

    int test(const char* zFile)
    {
        int rv = EXIT_FAILURE;

        m_file = zFile;

        FILE* pFile = fopen(m_file.c_str(), "r");

        if (pFile)
        {
            m_parser.set_sql_mode(mxs::Parser::SqlMode::DEFAULT);

            rv = test(pFile);

            fclose(pFile);
        }
        else
        {
            auto e = errno;
            cerr << "error: Could not open '" << zFile << "': " << strerror(e) << endl;
        }

        return rv;
    }

private:
    int test(FILE* pFile)
    {
        int rv = EXIT_SUCCESS;

        json_error_t error;
        json_t* pJson;

        m_line = 0;

        do
        {
            pJson = json_loadf(pFile, JSON_DISABLE_EOF_CHECK, &error);

            if (pJson)
            {
                // Not necessarily just a single line, if the json-object happens to be spread out over multiple lines, which is quite possible.
                ++m_line;

                rv = test(pJson);

                json_decref(pJson);
            }
        }
        while (pJson);

        if (!feof(pFile))
        {
            cerr << "error: (" << error.line << ", " << error.column << "), " << error.text << endl;
            rv = EXIT_FAILURE;
        }

        return rv;
    }

    int test(json_t* pJson)
    {
        int rv = EXIT_SUCCESS;

        json_t* pStmt = json_object_get(pJson, "statement");
        json_t* pSql_mode = json_object_get(pJson, "sql_mode");
        json_t* pClassification = json_object_get(pJson, "classification");

        if (pStmt && pSql_mode && pClassification
            && json_is_string(pStmt)
            && json_is_string(pSql_mode)
            && json_is_object(pClassification))
        {
            rv = test(json_string_value(pStmt), json_string_value(pSql_mode), pClassification);
        }
        else
        {
            cerr << "error: Json object '" << mxb::json_dump(pJson, 0)
                 << "' lacks 'statement', 'sql_mode' and/or 'classification', "
                 << "or they are not of correct type." << endl;

            rv = EXIT_FAILURE;
        }

        return rv;
    }

    int test(const char* zStmt, const char* zSql_mode, json_t* pClassification)
    {
        int rv = EXIT_FAILURE;

        Parser::SqlMode sql_mode;

        if (Parser::from_string(zSql_mode, &sql_mode))
        {
            m_parser.set_sql_mode(sql_mode);

            GWBUF stmt = m_parser.helper().create_packet(zStmt);

            rv = test(stmt, pClassification);
        }
        else
        {
            cerr << "error: '" << zSql_mode << "' is not a valid SqlMode." << endl;
        }

        return rv;
    }

    int test(const GWBUF& stmt, json_t* pClassification)
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
        errors += !check_type_mask(stmt, pClassification);
        errors += !check_relates_to_previous(stmt, pClassification);
        errors += !check_is_multi_stmt(stmt, pClassification);

        return errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
};

}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit(argv[0]);
    }

    int rv = EXIT_SUCCESS;

    mxs::set_datadir("/tmp");
    mxs::set_langdir(".");
    mxs::set_process_datadir("/tmp");

    if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
    {
        ParserPlugin* pPlugin = get_plugin("pp_sqlite", Parser::SqlMode::DEFAULT, "");

        if (pPlugin)
        {
            const Parser::Helper& helper = pPlugin->default_helper();
            std::unique_ptr<Parser> sParser = pPlugin->create_parser(&helper);

            Tester tester(sParser.get());

            for (int i = 1; i < argc && rv == EXIT_SUCCESS; ++i)
            {
                rv = tester.test(argv[i]);
            }

            put_plugin(pPlugin);
        }
    }
    else
    {
        cerr << "error: Could not initialize log." << endl;
    }

    return rv;
}
