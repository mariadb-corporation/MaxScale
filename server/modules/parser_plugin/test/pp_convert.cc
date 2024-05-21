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

#include <fstream>
#include <iostream>
#include <maxscale/paths.hh>
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

    cerr << "usage: " << zName << " file..." << endl;
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

    Converter(Parser* pParser)
        : ParserUtil(pParser)
    {
    }

    int convert_file(const std::string& file, ostream& out)
    {
        int rv = EXIT_FAILURE;

        ifstream in(file);

        if (in)
        {
            m_file = file;

            rv = convert_stream(in, out);
        }
        else
        {
            cerr << "error: Could not open " << file << " for reading." << endl;
        }

        return rv;
    }

private:
    int convert_stream(istream& in, ostream& out)
    {
        mxs::TestReader reader(mxs::TestReader::Expect::MARIADB, in);

        mxs::TestReader::result_t result;
        do
        {
            string statement;
            result = reader.get_statement(statement);

            m_line = reader.line();

            if (result == mxs::TestReader::RESULT_STMT)
            {
                cout << statement << endl;

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
            char* zJson = json_dumps(pJson, JSON_INDENT(2));

            out << zJson << "\n" << endl;

            free(zJson);
            json_decref(pJson);

            rv = true;
        }
        else
        {
            cerr << "error: Could not parse statement: " << statement << endl;
        }

        return rv;
    }

    json_t* convert_statement(const string& statement,
                              const GWBUF& packet,
                              Parser::Result result) const
    {
        json_t* pJson = json_object();
        json_object_set_new(pJson, "statement", json_string(statement.c_str()));
        json_object_set_new(pJson, "result", json_string(Parser::to_string(result)));
        json_object_set_new(pJson, "sql_mode", json_string(Parser::to_string(m_parser.get_sql_mode())));
        json_object_set_new(pJson, "classification", get_classification(packet));

        return pJson;
    }
};

int convert(Parser& parser, const string& from, const string& to)
{
    int rv = EXIT_FAILURE;

    ofstream out(to, std::ios::trunc);

    if (out)
    {
        Converter converter(&parser);

        rv = converter.convert_file(from, out);
    }
    else
    {
        cerr << "error: Could no open " << to << " for writing." << endl;
    }

    return rv;
}

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

            for (int i = 1; i < argc && rv == EXIT_SUCCESS; ++i)
            {
                sParser->set_sql_mode(mxs::Parser::SqlMode::DEFAULT);

                string from = argv[i];
                string to;

                auto j = from.rfind(".test");

                if (j == from.length() - 5)
                {
                    to = from.substr(0, from.length() - 5) + ".json";
                }
                else
                {
                    cout << "warning: '" << from << "' does not end with '.test', "
                         << "suffix '.json' will simply be appended." << endl;
                    to = from + ".json";
                }

                rv = convert(*sParser, from, to);
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
