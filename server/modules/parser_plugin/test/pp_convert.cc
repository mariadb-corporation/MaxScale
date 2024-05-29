/*
 * Copyright (c) 2025 MariaDB plc
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

#include <fstream>
#include <iostream>
#include <unistd.h>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>
#include "setsqlmodeparser.hh"
#include "testreader.hh"
#include "utils.hh"

using namespace std;

using mxs::Parser;
using mxs::ParserPlugin;

namespace
{

void print_usage_and_exit(const char* zName)
{
    cerr << "usage: " << zName << " [-0 parser_plugin] [-m (default|oracle)] [-v verbosity] file...\n"
         << "\n"
         << "-0    Parser plugin to use, default is 'pp_sqlite'\n"
         << "-m    In which sql-mode to start, default is 'default'\n"
         << "-v 0  Print nothing.\n"
         << "   1  Print name of file being converted, default.\n"
         << "   2  Print name of file being converted and all statements.\n"
         << "\n"
         << "If no file is provided, the input will be read from stdin."
         << flush;

    exit(EXIT_FAILURE);
}

}

namespace
{


class Converter : public ParserUtil
{
public:
    Converter(const Converter&) = delete;
    Converter& operator = (const Converter&) = delete;

    Converter(Parser* pParser, Parser::SqlMode sql_mode, Verbosity verbosity)
        : ParserUtil(pParser, sql_mode, verbosity)
    {
    }

    int convert(const std::string& file, ostream& out)
    {
        int rv = EXIT_FAILURE;

        ifstream in(file);

        if (in)
        {
            m_file = file;

            if (m_verbosity > Verbosity::MIN)
            {
                cout << m_file << endl;
            }

            rv = convert_stream(in, out);
        }
        else
        {
            cerr << "error: Could not open '" << file << "' for reading." << endl;
        }

        return rv;
    }

    int convert(istream& in, ostream& out)
    {
        m_file = "stream";

        return convert_stream(in, out);
    }

private:
    int convert_stream(istream& in, ostream& out)
    {
        m_parser.set_sql_mode(m_sql_mode);

        mxs::TestReader reader(mxs::TestReader::Expect::MARIADB, in);

        mxs::TestReader::result_t result;
        do
        {
            string statement;
            result = reader.get_statement(statement);

            m_line = reader.line();

            if (result == mxs::TestReader::RESULT_STMT)
            {
                if (m_verbosity > Verbosity::NORMAL)
                {
                    cout << statement << endl;
                }

                SetSqlModeParser::sql_mode_t sql_mode;
                SetSqlModeParser sql_mode_parser;

                if (sql_mode_parser.get_sql_mode(statement, &sql_mode) == SetSqlModeParser::IS_SET_SQL_MODE)
                {
                    switch (sql_mode)
                    {
                    case SetSqlModeParser::DEFAULT:
                        m_parser.set_sql_mode(Parser::SqlMode::DEFAULT);
                        break;

                    case SetSqlModeParser::ORACLE:
                        m_parser.set_sql_mode(Parser::SqlMode::ORACLE);
                        break;

                    case SetSqlModeParser::SOMETHING:
                        break;

                    default:
                        mxb_assert(!true);
                    }
                }

                if (!convert_statement(statement, out))
                {
                    result = mxs::TestReader::RESULT_ERROR;
                }
            }
        }
        while (result == mxs::TestReader::RESULT_STMT);

        return result == mxs::TestReader::RESULT_EOF ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    bool convert_statement(const string& statement, ostream& out)
    {
        bool rv = false;
        GWBUF packet = m_parser.helper().create_packet(statement);

        auto result = m_parser.parse(packet, Parser::COLLECT_ALL);

        if (result != Parser::Result::INVALID)
        {
            json_t* pJson = convert_statement(statement, packet, result);

            if (pJson)
            {
                char* zJson = json_dumps(pJson, JSON_INDENT(2));

                out << zJson << "\n" << endl;

                free(zJson);
                json_decref(pJson);
            }

            rv = true;
        }
        else
        {
            cerr << prefix() << "Could not parse statement: " << statement << endl;
        }

        return rv;
    }

    json_t* convert_statement(const string& statement,
                              const GWBUF& packet,
                              Parser::Result result) const
    {
        json_t* pJson = nullptr;
        json_t* pStatement = json_string(statement.c_str());
        const char* zStatement = "statement";

        if (!pStatement)
        {
            cerr << prefix("warning") << "The string '" << statement << "' could not be turned into a "
                 << "JSON string. Storing it base64-encoded instead." << endl;

            const uint8_t* p = reinterpret_cast<const uint8_t*>(statement.data());
            string statement_base64 = mxs::to_base64(p, statement.length());

            pStatement = json_string(statement_base64.c_str());
            mxb_assert(pStatement);
            zStatement = "statement_base64";
        }

        pJson = json_object();
        json_object_set_new(pJson, zStatement, pStatement);
        json_object_set_new(pJson, "result", json_string(Parser::to_string(result)));
        json_object_set_new(pJson, "sql_mode", json_string(Parser::to_string(m_parser.get_sql_mode())));
        json_object_set_new(pJson, "classification", get_classification(packet));

        return pJson;
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

            Converter converter(sParser.get(), sql_mode, verbosity);

            if (argc - optind == 0)
            {
                rv = converter.convert(cin, cout);
            }
            else
            {
                for (int i = optind; i < argc && rv == EXIT_SUCCESS; ++i)
                {
                    string from = argv[i];
                    string to;

                    auto j = from.rfind(".test");

                    if (j == from.length() - 5)
                    {
                        to = from.substr(0, from.length() - 5) + ".pptest";
                    }
                    else
                    {
                        cout << "warning: '" << from << "' does not end with '.test', "
                             << "suffix '.pptest' will simply be appended." << endl;
                        to = from + ".pptest";
                    }

                    ofstream out(to, std::ios::trunc);

                    if (out)
                    {
                        rv = converter.convert(from, out);
                    }
                    else
                    {
                        cerr << "error: Could not open " << to << " for writing." << endl;
                        rv = EXIT_FAILURE;
                    }
                }
            }

            put_plugin(pPlugin);
        }
        else
        {
            rv = EXIT_FAILURE;
        }
    }
    else
    {
        cerr << "error: Could not initialize log." << endl;
    }

    return rv;
}
