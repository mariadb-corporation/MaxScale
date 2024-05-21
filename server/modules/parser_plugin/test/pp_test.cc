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

        ifstream in(m_file);

        if (in)
        {
            m_parser.set_sql_mode(mxs::Parser::SqlMode::DEFAULT);

            rv = test(in);
        }
        else
        {
            auto e = errno;
            cerr << "error: Could not open '" << m_file << "'." << endl;
        }

        return rv;
    }

private:
    string prefix() const
    {
        stringstream ss;
        ss << "error: (" << m_file << ", " << m_line << "): ";
        return ss.str();
    }

    int test(istream& in)
    {
        int rv = EXIT_SUCCESS;

        json_t* pJson;

        m_line = 0;

        string json;

        do
        {
            rv = get_json(in, &json);

            if (rv == EXIT_SUCCESS && !json.empty())
            {
                json_error_t error;
                pJson = json_loadb(json.data(), json.length(), 0, &error);

                if (pJson)
                {
                    rv = test(pJson);

                    json_decref(pJson);
                }
                else
                {
                    cerr << prefix() << "'XXX" << json << "XXX' is not valid JSON: " << error.text << endl;
                    rv = EXIT_FAILURE;
                }
            }
        }
        while (rv == EXIT_SUCCESS && !json.empty());

        return rv;
    }

    string get_line(istream& in)
    {
        string line;

        do
        {
            if (std::getline(in, line))
            {
                ++m_line;
            }

            string trimmed = mxb::trimmed_copy(line);

            if (trimmed.empty())
            {
                line.clear();
            }
            else
            {
                // A line with '#' as first non-space character is treated as a comment.
                if (trimmed.front() == '#')
                {
                    line.clear();
                }
            }
        }
        while (line.empty() && in);

        return line;
    }

    int get_json(istream& in, string* pJson)
    {
        int rv = EXIT_SUCCESS;

        string json;

        int nBraces = 0;

        do
        {
            string line = get_line(in);

            if (line.empty())
            {
                continue;
            }

            if (nBraces == 0 && line.front() != '{')
            {
                cerr << prefix() << "'" << line << "' does not appear to be the beginning of a JSON object."
                     << endl;
                rv = EXIT_FAILURE;
                break;
            }

            bool escaped = false;
            bool in_string = false;

            auto it = line.begin();
            for (; it != line.end(); ++it)
            {
                char c = *it;

                switch (c)
                {
                case '{':
                    if (!in_string)
                    {
                        ++nBraces;
                    }
                    break;

                case '}':
                    if (!in_string)
                    {
                        --nBraces;
                        mxb_assert(nBraces >= 0);
                    }
                    break;

                case '\\':
                    if (in_string)
                    {
                        escaped = !escaped;
                    }
                    break;

                case '"':
                    if (!in_string)
                    {
                        in_string = true;
                    }
                    else if (escaped)
                    {
                        escaped = false;
                    }
                    else
                    {
                        in_string = false;
                    }
                    break;

                default:
                    escaped = false;
                }

                if (nBraces == 0)
                {
                    break;
                }
            }

            if (!json.empty())
            {
                json += '\n';
            }

            json += line;

            if (nBraces == 0 && it + 1 != line.end())
            {
                string tail(it + 1, line.end());

                mxb::trim(tail);

                if (!tail.empty())
                {
                    cerr << prefix() << "Trailing garbage: '" << json << "'" << endl;
                    rv = EXIT_FAILURE;
                }
            }
        }
        while (rv == EXIT_SUCCESS && nBraces != 0 && in);

        if (rv == EXIT_SUCCESS)
        {
            pJson->swap(json);
        }

        return rv;
    }

    int test(json_t* pJson)
    {
        int rv = EXIT_SUCCESS;

        json_t* pStmt = json_object_get(pJson, "statement");
        json_t* pResult = json_object_get(pJson, "result");
        json_t* pSql_mode = json_object_get(pJson, "sql_mode");
        json_t* pClassification = json_object_get(pJson, "classification");

        if (pStmt && pResult && pSql_mode && pClassification
            && json_is_string(pStmt)
            && json_is_string(pResult)
            && json_is_string(pSql_mode)
            && json_is_object(pClassification))
        {
            const char* zStmt = json_string_value(pStmt);
            const char* zResult = json_string_value(pResult);
            const char* zSql_mode = json_string_value(pSql_mode);

            rv = test(zStmt, zResult, zSql_mode, pClassification);
        }
        else
        {
            cerr << prefix() << "Json object '" << mxb::json_dump(pJson, 0)
                 << "' lacks 'statement', 'result', 'sql_mode' and/or 'classification', "
                 << "or they are not of correct type." << endl;

            rv = EXIT_FAILURE;
        }

        return rv;
    }

    int test(const char* zStmt, const char* zResult, const char* zSql_mode, json_t* pClassification)
    {
        int rv = EXIT_FAILURE;

        Parser::Result expected_result;
        if (Parser::from_string(zResult, &expected_result))
        {
            Parser::SqlMode sql_mode;
            if (Parser::from_string(zSql_mode, &sql_mode))
            {
                m_parser.set_sql_mode(sql_mode);

                GWBUF stmt = m_parser.helper().create_packet(zStmt);

                rv = test(expected_result, stmt, pClassification);
            }
            else
            {
                cerr << prefix() << "'" << zSql_mode << "' is not a valid Parser::SqlMode." << endl;
            }
        }
        else
        {
            cerr << prefix() << "'" << zResult << "' is not a valid Parser::Result." << endl;
        }

        return rv;
    }

    int test(Parser::Result expected_result, const GWBUF& stmt, json_t* pClassification)
    {
        int errors = 0;

        auto result = m_parser.parse(stmt, Parser::COLLECT_ALL);

        if (result < expected_result)
        {
            cerr << prefix() << ": Expected of parsing at least " << Parser::to_string(expected_result)
                 << ", but got " << Parser::to_string(result) << "." << endl;

            ++errors;
        }

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
