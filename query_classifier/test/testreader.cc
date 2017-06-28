/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "testreader.hh"
#include <algorithm>
#include <map>
#include <iostream>

using std::istream;
using std::string;
using std::map;

namespace
{

enum skip_action_t
{
    SKIP_NOTHING,        // Skip nothing.
    SKIP_BLOCK,          // Skip until the end of next { ... }
    SKIP_DELIMITER,      // Skip the new delimiter.
    SKIP_LINE,           // Skip current line.
    SKIP_NEXT_STATEMENT, // Skip statement starting on line following this line.
    SKIP_STATEMENT,      // Skip statment starting on this line.
    SKIP_TERMINATE,      // Cannot handle this, terminate.
};

typedef std::map<std::string, skip_action_t> KeywordActionMapping;

static KeywordActionMapping mtl_keywords;

void init_keywords()
{
    struct Keyword
    {
        const char* z_keyword;
        skip_action_t action;
    };

    static const Keyword KEYWORDS[] =
    {
        { "append_file",                SKIP_LINE },
        { "cat_file",                   SKIP_LINE },
        { "change_user",                SKIP_LINE },
        { "character_set",              SKIP_LINE },
        { "chmod",                      SKIP_LINE },
        { "connect",                    SKIP_LINE },
        { "connection",                 SKIP_LINE },
        { "copy_file",                  SKIP_LINE },
        { "dec",                        SKIP_LINE },
        { "delimiter",                  SKIP_DELIMITER },
        { "die",                        SKIP_LINE },
        { "diff_files",                 SKIP_LINE },
        { "dirty_close",                SKIP_LINE },
        { "disable_abort_on_error",     SKIP_LINE },
        { "disable_connect_log",        SKIP_LINE },
        { "disable_info",               SKIP_LINE },
        { "disable_metadata",           SKIP_LINE },
        { "disable_parsing",            SKIP_LINE },
        { "disable_ps_protocol",        SKIP_LINE },
        { "disable_query_log",          SKIP_LINE },
        { "disable_reconnect",          SKIP_LINE },
        { "disable_result_log",         SKIP_LINE },
        { "disable_rpl_parse",          SKIP_LINE },
        { "disable_session_track_info", SKIP_LINE },
        { "disable_warnings",           SKIP_LINE },
        { "disconnect",                 SKIP_LINE },
        { "echo",                       SKIP_LINE },
        { "enable_abort_on_error",      SKIP_LINE },
        { "enable_connect_log",         SKIP_LINE },
        { "enable_info",                SKIP_LINE },
        { "enable_metadata",            SKIP_LINE },
        { "enable_parsing",             SKIP_LINE },
        { "enable_ps_protocol",         SKIP_LINE },
        { "enable_query_log",           SKIP_LINE },
        { "enable_reconnect",           SKIP_LINE },
        { "enable_result_log",          SKIP_LINE },
        { "enable_rpl_parse",           SKIP_LINE },
        { "enable_session_track_info",  SKIP_LINE },
        { "enable_warnings",            SKIP_LINE },
        { "end_timer",                  SKIP_LINE },
        { "error",                      SKIP_NEXT_STATEMENT },
        { "eval",                       SKIP_STATEMENT },
        { "exec",                       SKIP_LINE },
        { "exit",                       SKIP_LINE },
        { "file_exists",                SKIP_LINE },
        { "horizontal_results",         SKIP_LINE },
        { "if",                         SKIP_BLOCK },
        { "inc",                        SKIP_LINE },
        { "let",                        SKIP_LINE },
        { "let",                        SKIP_LINE },
        { "list_files",                 SKIP_LINE },
        { "list_files_append_file",     SKIP_LINE },
        { "list_files_write_file",      SKIP_LINE },
        { "lowercase_result",           SKIP_LINE },
        { "mkdir",                      SKIP_LINE },
        { "move_file",                  SKIP_LINE },
        { "output",                     SKIP_LINE },
        { "perl",                       SKIP_TERMINATE },
        { "ping",                       SKIP_LINE },
        { "print",                      SKIP_LINE },
        { "query",                      SKIP_LINE },
        { "query_get_value",            SKIP_LINE },
        { "query_horizontal",           SKIP_LINE },
        { "query_vertical",             SKIP_LINE },
        { "real_sleep",                 SKIP_LINE },
        { "reap",                       SKIP_LINE },
        { "remove_file",                SKIP_LINE },
        { "remove_files_wildcard",      SKIP_LINE },
        { "replace_column",             SKIP_LINE },
        { "replace_regex",              SKIP_LINE },
        { "replace_result",             SKIP_LINE },
        { "require",                    SKIP_LINE },
        { "reset_connection",           SKIP_LINE },
        { "result",                     SKIP_LINE },
        { "result_format",              SKIP_LINE },
        { "rmdir",                      SKIP_LINE },
        { "same_master_pos",            SKIP_LINE },
        { "send",                       SKIP_LINE },
        { "send_eval",                  SKIP_LINE },
        { "send_quit",                  SKIP_LINE },
        { "send_shutdown",              SKIP_LINE },
        { "skip",                       SKIP_LINE },
        { "sleep",                      SKIP_LINE },
        { "sorted_result",              SKIP_LINE },
        { "source",                     SKIP_LINE },
        { "start_timer",                SKIP_LINE },
        { "sync_slave_with_master",     SKIP_LINE },
        { "sync_with_master",           SKIP_LINE },
        { "system",                     SKIP_LINE },
        { "vertical_results",           SKIP_LINE },
        { "while",                      SKIP_BLOCK },
        { "write_file",                 SKIP_LINE },
    };

    const size_t N_KEYWORDS = sizeof(KEYWORDS)/sizeof(KEYWORDS[0]);

    for (size_t i = 0; i < N_KEYWORDS; ++i)
    {
        mtl_keywords[KEYWORDS[i].z_keyword] = KEYWORDS[i].action;
    }
}

skip_action_t get_action(const string& keyword, const string& delimiter)
{
    skip_action_t action = SKIP_NOTHING;

    string key(keyword);
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    if (key == "delimiter")
    {
        // DELIMITER is directly understood by the parser so it needs to
        // be handled explicitly.
        action = SKIP_DELIMITER;
    }
    else if (delimiter == ";")
    {
        // Some mysqltest keywords, such as "while", "exit" and "if" are also
        // PL/SQL keywords. We assume they can only be used in the former role,
        // if the delimiter is ";".
        string key(keyword);

        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        KeywordActionMapping::iterator i = mtl_keywords.find(key);

        if (i != mtl_keywords.end())
        {
            action = i->second;
        }
    }

    return action;
}

inline void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
}

inline void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
}

void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

}

namespace maxscale
{

TestReader::TestReader(istream& in,
                       size_t   line)
    : m_in(in)
    , m_line(line)
    , m_delimiter(";")
{
    init();
}

TestReader::result_t TestReader::get_statement(std::string& stmt)
{
    bool error = false; // Whether an error has occurred.
    bool found = false; // Whether we have found a statement.
    bool skip = false;  // Whether next statement should be skipped.

    stmt.clear();

    string line;

    while (!error && !found && std::getline(m_in, line))
    {
        trim(line);

        m_line++;

        if (!line.empty() && (line.at(0) != '#'))
        {
            // Ignore comment lines.
            if ((line.substr(0, 3) == "-- ") || (line.substr(0, 1) == "#"))
            {
                continue;
            }

            if (!skip)
            {
                if (line.substr(0, 2) == "--")
                {
                    line = line.substr(2);
                    trim(line);
                }

                string::iterator i = std::find_if(line.begin(), line.end(),
                                                  std::ptr_fun<int,int>(std::isspace));
                string keyword = line.substr(0, i - line.begin());

                skip_action_t action = get_action(keyword, m_delimiter);

                switch (action)
                {
                case SKIP_NOTHING:
                    break;

                case SKIP_BLOCK:
                    skip_block();
                    continue;

                case SKIP_DELIMITER:
                    line = line.substr(i - line.begin());
                    trim(line);
                    if (line.length() > 0)
                    {
                        if (line.length() >= m_delimiter.length())
                        {
                            if (line.substr(line.length() - m_delimiter.length()) == m_delimiter)
                            {
                                m_delimiter = line.substr(0, line.length() - m_delimiter.length());
                            }
                            else
                            {
                                m_delimiter = line;
                            }
                        }
                        else
                        {
                            m_delimiter = line;
                        }
                    }
                    continue;

                case SKIP_LINE:
                    continue;

                case SKIP_NEXT_STATEMENT:
                    skip = true;
                    continue;

                case SKIP_STATEMENT:
                    skip = true;
                    break;

                case SKIP_TERMINATE:
                    MXS_ERROR("Cannot handle line %u: %s", (unsigned)m_line, line.c_str());
                    error = true;
                    break;
                }
            }

            stmt += line;

            // Look for a ';'. If we are dealing with a one line test statment
            // the delimiter will in practice be ';' and if it is a multi-line
            // test statement then the test-script delimiter will be something
            // else than ';' and ';' will be the delimiter used in the multi-line
            // statement.
            string::size_type i = line.find(";");

            if (i != string::npos)
            {
                // Is there a "-- " or "#" after the delimiter?
                if ((line.find("-- ", i) != string::npos) ||
                    (line.find("#", i) != string::npos))
                {
                    // If so, add a newline. Otherwise the rest of the
                    // statement would be included in the comment.
                    stmt += "\n";
                }

                // This is somewhat fragile as a ";", "#" or "-- " inside a
                // string will trigger this behaviour...
            }

            string c;

            if (line.length() >= m_delimiter.length())
            {
                c = line.substr(line.length() - m_delimiter.length());
            }

            if (c == m_delimiter)
            {
                if (c != ";")
                {
                    // If the delimiter was something else but ';' we need to
                    // remove that before giving the line to the classifiers.
                    stmt.erase(stmt.length() - m_delimiter.length());
                }

                if (!skip)
                {
                    found = true;
                }
                else
                {
                    skip = false;
                    stmt.clear();
                }
            }
            else if (!skip)
            {
                stmt += " ";
            }
        }
        else if (line.substr(0, 7) == "--error")
        {
            // Next statement is supposed to fail, no need to check.
            skip = true;
        }
    }

    result_t result;

    if (error)
    {
        result = RESULT_ERROR;
    }
    else if (found)
    {
        result = RESULT_STMT;
    }
    else
    {
        result = RESULT_EOF;
    }

    return result;
}

// static
void TestReader::init()
{
    static bool inited = false;

    if (!inited)
    {
        inited = true;

        init_keywords();
    }
}

void TestReader::skip_block()
{
    int c;

    // Find first '{'
    while (m_in && ((c = m_in.get()) != '{'))
    {
        if (c == '\n')
        {
            ++m_line;
        }
    }

    int n = 1;

    while ((n > 0) && m_in)
    {
        c = m_in.get();

        switch (c)
        {
        case '{':
            ++n;
            break;

        case '}':
            --n;
            break;

        case '\n':
            ++m_line;
            break;

        default:
            ;
        }
    }
}

}
