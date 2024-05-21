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
    cerr << "usage: " << zName << " [-0 parser_plugin] [-m (default|oracle)] [-v verbosity] file*\n"
         << "\n"
         << "-0    Parser plugin to use, default is 'pp_sqlite'\n"
         << "-m    In which sql-mode to start, default is 'default'\n"
         << "-v 0  Print nothing.\n"
         << "   1  Print name of file being tested, default.\n"
         << "   2  Print name of file being tested and all statements.\n"
         << "\n"
         << "If no file is provided, the input will be read from stdin."
         << flush;

    exit(EXIT_FAILURE);
}

}

namespace
{

class Tester : public ParserUtil
{
public:
    Tester(Parser* pParser, Parser::SqlMode sql_mode, Verbosity verbosity)
        : ParserUtil(pParser, sql_mode, verbosity)
    {
    }

    int test(const char* zFile)
    {
        int rv = EXIT_FAILURE;

        ifstream in(zFile);

        if (in)
        {
            m_file = zFile;

            if (m_verbosity > Verbosity::MIN)
            {
                cout << m_file << endl;
            }

            rv = test_stream(in);
        }
        else
        {
            cerr << "error: Could not open '" << zFile << "' for reading." << endl;
        }

        return rv;
    }

    int test(istream& in)
    {
        m_file = "stream";

        return test_stream(in);
    }

private:
    int test_stream(istream& in)
    {
        int rv = EXIT_SUCCESS;

        m_parser.set_sql_mode(m_sql_mode);

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
                    if (m_verbosity > Verbosity::NORMAL)
                    {
                        cout << json_string_value(json_object_get(pJson, "statement")) << endl;
                    }

                    rv = test(pJson);

                    json_decref(pJson);
                }
                else
                {
                    cerr << prefix() << "'" << json << "' is not valid JSON: " << error.text << endl;
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

        bool base64 = false;
        json_t* pStmt = json_object_get(pJson, "statement");
        json_t* pResult = json_object_get(pJson, "result");
        json_t* pSql_mode = json_object_get(pJson, "sql_mode");
        json_t* pClassification = json_object_get(pJson, "classification");

        if (!pStmt)
        {
            pStmt = json_object_get(pJson, "statement_base64");
            base64 = true;
        }

        if (pStmt && pResult && pSql_mode && pClassification
            && json_is_string(pStmt)
            && json_is_string(pResult)
            && json_is_string(pSql_mode)
            && json_is_object(pClassification))
        {
            string stmt = json_string_value(pStmt);

            if (base64)
            {
                vector<uint8_t> v = mxs::from_base64(stmt);

                stmt.assign(v.begin(), v.end());
            }

            const char* zResult = json_string_value(pResult);
            const char* zSql_mode = json_string_value(pSql_mode);

            rv = test(stmt.c_str(), zResult, zSql_mode, pClassification);
        }
        else
        {
            cerr << prefix() << "Json object '" << mxb::json_dump(pJson, 0)
                 << "' lacks 'statement' or 'statement_base64', 'result', 'sql_mode' "
                 << "and/or 'classification', or they are not of correct type." << endl;

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
    int rv = EXIT_SUCCESS;

    ParserUtil::Verbosity verbosity = ParserUtil::Verbosity::NORMAL;
    const char* zParser_plugin = "pp_sqlite";
    Parser::SqlMode sql_mode = Parser::SqlMode::DEFAULT;

    int c;
    while ((c = getopt(argc, argv, "0:m:v:")) != -1)
    {
        switch (c)
        {
        case '0':
            zParser_plugin = optarg;
            break;

        case 'm':
            if (strcasecmp(optarg, "default") == 0)
            {
                sql_mode = Parser::SqlMode::DEFAULT;
            }
            else if (strcasecmp(optarg, "oracle") == 0)
            {
                sql_mode = Parser::SqlMode::ORACLE;
            }
            else
            {
                rv = EXIT_FAILURE;
                break;
            }
            break;

        case 'v':
            {
                int v = atoi(optarg);
                if (v >= (int)ParserUtil::Verbosity::MIN && v <= (int)ParserUtil::Verbosity::MAX)
                {
                    verbosity = static_cast<ParserUtil::Verbosity>(v);
                }
                else
                {
                    rv = EXIT_FAILURE;
                }
            }
            break;

        default:
            rv = EXIT_FAILURE;
        }
    }

    if (rv == EXIT_FAILURE)
    {
        print_usage_and_exit(argv[0]);
    }

    mxs::set_datadir("/tmp");
    mxs::set_langdir(".");
    mxs::set_process_datadir("/tmp");

    if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
    {
        ParserPlugin* pPlugin = get_plugin(zParser_plugin, sql_mode, "");

        if (pPlugin)
        {
            const Parser::Helper& helper = pPlugin->default_helper();
            std::unique_ptr<Parser> sParser = pPlugin->create_parser(&helper);

            Tester tester(sParser.get(), sql_mode, verbosity);

            if (argc - optind == 0)
            {
                rv = tester.test(cin);
            }
            else
            {
                for (int i = optind; i < argc && rv == EXIT_SUCCESS; ++i)
                {
                    rv = tester.test(argv[i]);
                }
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
