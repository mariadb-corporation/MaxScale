/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <unistd.h>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <sstream>
#include <maxbase/string.hh>
#include <maxscale/paths.hh>
#include <maxscale/log.hh>
#include <maxscale/parser.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#undef MXB_MODULE_NAME
#include "../../server/modules/protocol/Postgres/pgparser.hh"
#include "../../server/core/internal/modules.hh"
#include "setsqlmodeparser.hh"
#include "testreader.hh"

using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::ifstream;
using std::istream;
using std::map;
using std::ostream;
using std::set;
using std::string;
using std::stringstream;

using mxs::Parser;
using mxs::ParserPlugin;
namespace sql = mxs::sql;

namespace
{

char USAGE[] =
    "usage: compare [-r count] [-d] [-0 classifier] [-1 classifier1] [-2 classifier2] "
    "[-A args] [-B args] [-C args] [-m [default|oracle]] [-v [0..2]] [-H (postgres|mariadb)] "
    "[-p properties] [-x regex] [-c regex] [-s statement]|[file+]]\n\n"
    "-r    redo the test the specified number of times; 0 means forever, default is 1\n"
    "-d    don't stop after first failed query\n"
    "-0    sanity check mode, compares the statement twice with the same classifier\n"
    "-1    the first classifier, default 'qc_mysqlembedded'\n"
    "-2    the second classifier, default 'qc_sqlite'\n"
    "-A    arguments for the first classifier\n"
    "-B    arguments for the second classifier\n"
    "-C    arguments for both classifiers\n"
    "-m    initial sql mode, 'default' or 'oracle', default is 'default'\n"
    "-s    compare single statement\n"
    "-S    strict, also require that the parse result is identical\n"
    "-R    strict reporting, report if parse result is different\n"
    "-x    test only statements matching the regex\n"
    "-H    use MariaDB or Postgres Parser helper, default 'mariadb'\n"
    "-p    only test and print properties\n"
    "-c    check that response matches regex (type and operation)\n"
    "-v 0, only return code\n"
    "   1, query and result for failed cases\n"
    "   2, all queries, and result for failed cases\n"
    "   3, all queries and all results\n";


enum verbosity_t
{
    VERBOSITY_MIN,
    VERBOSITY_NORMAL,
    VERBOSITY_EXTENDED,
    VERBOSITY_MAX
};

struct State
{
    bool            query_printed;
    string          query;
    verbosity_t     verbosity;
    bool            result_printed;
    bool            stop_at_error;
    bool            strict;
    bool            strict_reporting;
    size_t          line;
    size_t          n_statements;
    size_t          n_errors;
    struct timespec time1;
    struct timespec time2;
    string          indent;
} global = {false,              // query_printed
            "",                 // query
            VERBOSITY_NORMAL,   // verbosity
            false,              // result_printed
            true,               // stop_at_error
            false,              // strict
            false,              // strict reporting
            0,                  // line
            0,                  // n_statements
            0,                  // n_errors
            {0, 0},             // time1
            {0, 0},             // time2
            ""                  // indent
};

ostream& operator<<(ostream& out, Parser::Result x)
{
    out << mxs::parser::to_string(x);
    return out;
}

ParserPlugin* load_plugin(const char* name)
{
    bool loaded = false;
    size_t len = strlen(name);
    char libdir[len + 3 + 1];   // Extra for ../

    sprintf(libdir, "../%s", name);

    mxs::set_libdir(libdir);

    ParserPlugin* pPlugin = ParserPlugin::load(name);

    if (!pPlugin)
    {
        cerr << "error: Could not load classifier " << name << "." << endl;
    }

    return pPlugin;
}

ParserPlugin* get_plugin(const char* zName, Parser::SqlMode sql_mode, const char* zArgs)
{
    ParserPlugin* pPlugin = nullptr;

    if (zName)
    {
        pPlugin = load_plugin(zName);

        if (pPlugin)
        {
            if (!pPlugin->setup(sql_mode, zArgs) || !pPlugin->thread_init())
            {
                cerr << "error: Could not setup or init classifier " << zName << "." << endl;
                ParserPlugin::unload(pPlugin);
                pPlugin = 0;
            }
        }
    }

    return pPlugin;
}

void put_plugin(ParserPlugin* pPlugin)
{
    if (pPlugin)
    {
        pPlugin->thread_end();
        ParserPlugin::unload(pPlugin);
    }
}

bool get_plugins(Parser::SqlMode sql_mode,
                 const char* zName1,
                 const char* zArgs1,
                 ParserPlugin** ppPlugin1,
                 const char* zName2,
                 const char* zArgs2,
                 ParserPlugin** ppPlugin2)
{
    bool rc = false;

    ParserPlugin* pPlugin1 = get_plugin(zName1, sql_mode, zArgs1);
    ParserPlugin* pPlugin2 = get_plugin(zName2, sql_mode, zArgs2);

    if ((!zName1 || pPlugin1) && (!zName2 || pPlugin2))
    {
        *ppPlugin1 = pPlugin1;
        *ppPlugin2 = pPlugin2;
        rc = true;
    }
    else
    {
        put_plugin(pPlugin1);
        put_plugin(pPlugin2);
    }

    return rc;
}

void put_plugins(ParserPlugin* pPlugin1, ParserPlugin* pPlugin2)
{
    put_plugin(pPlugin1);
    put_plugin(pPlugin2);
}

void report_query()
{
    cout << "(" << global.line << "): " << global.query << endl;
    global.query_printed = true;
}

void report(bool success, const string& s)
{
    if (success)
    {
        if (global.verbosity >= VERBOSITY_NORMAL)
        {
            if (global.verbosity >= VERBOSITY_EXTENDED)
            {
                if (!global.query_printed)
                {
                    report_query();
                }

                if (global.verbosity >= VERBOSITY_MAX)
                {
                    cout << global.indent << s << endl;
                    global.result_printed = true;
                }
            }
        }
    }
    else
    {
        if (global.verbosity >= VERBOSITY_NORMAL)
        {
            if (!global.query_printed)
            {
                report_query();
            }

            cout << global.indent << s << endl;
            global.result_printed = true;
        }
    }
}

static timespec timespec_subtract(const timespec& later, const timespec& earlier)
{
    timespec result = {0, 0};

    mxb_assert((later.tv_sec > earlier.tv_sec)
               || ((later.tv_sec == earlier.tv_sec) && (later.tv_nsec > earlier.tv_nsec)));

    if (later.tv_nsec >= earlier.tv_nsec)
    {
        result.tv_sec = later.tv_sec - earlier.tv_sec;
        result.tv_nsec = later.tv_nsec - earlier.tv_nsec;
    }
    else
    {
        result.tv_sec = later.tv_sec - earlier.tv_sec - 1;
        result.tv_nsec = 1000000000 + later.tv_nsec - earlier.tv_nsec;
    }

    return result;
}

static void update_time(timespec* pResult, timespec& start, timespec& finish)
{
    timespec difference = timespec_subtract(finish, start);

    long nanosecs = pResult->tv_nsec + difference.tv_nsec;

    if (nanosecs > 1000000000)
    {
        pResult->tv_sec += 1;
        pResult->tv_nsec += (nanosecs - 1000000000);
    }
    else
    {
        pResult->tv_nsec = nanosecs;
    }

    pResult->tv_sec += difference.tv_sec;
}

bool compare_parse(const Parser& parser1,
                   const GWBUF& copy1,
                   const Parser& parser2,
                   const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_parse                 : ";

    struct timespec start;
    struct timespec finish;

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    Parser::Result rv1 = parser1.parse(copy1, Parser::COLLECT_ESSENTIALS);
    clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
    update_time(&global.time1, start, finish);

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    Parser::Result rv2 = parser2.parse(copy2, Parser::COLLECT_ESSENTIALS);
    clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
    update_time(&global.time2, start, finish);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << rv1;
        success = true;
    }
    else
    {
        if (global.strict)
        {
            ss << "ERR: ";
        }
        else
        {
            ss << "INF: ";
            if (!global.strict_reporting)
            {
                success = true;
            }
        }

        ss << rv1 << " != " << rv2;
    }

    report(success, ss.str());

    return success;
}

bool compare_get_type(const std::optional<std::regex>& check_regex,
                      const Parser& parser1,
                      const GWBUF& copy1,
                      const Parser& parser2,
                      const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_type_mask         : ";

    uint32_t rv1 = parser1.get_type_mask(copy1);
    uint32_t rv2 = parser2.get_type_mask(copy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {

        if (!check_regex || std::regex_search(Parser::type_mask_to_string(rv1), *check_regex))
        {
            ss << "Ok : " << Parser::type_mask_to_string(rv1);
            success = true;
        }
        else
        {
            ss << "NOT: "
               << Parser::type_mask_to_string(rv1)
               << " does NOT match regex.";
        }
    }
    else
    {
        uint32_t rv1b = rv1;

        if (rv1b & sql::TYPE_WRITE)
        {
            rv1b &= ~(uint32_t)sql::TYPE_READ;
        }

        uint32_t rv2b = rv2;

        if (rv2b & sql::TYPE_WRITE)
        {
            rv2b &= ~(uint32_t)sql::TYPE_READ;
        }

        if (rv1b & sql::TYPE_READ)
        {
            rv1b &= ~(uint32_t)sql::TYPE_LOCAL_READ;
        }

        if (rv2b & sql::TYPE_READ)
        {
            rv2b &= ~(uint32_t)sql::TYPE_LOCAL_READ;
        }

        auto types1 = Parser::type_mask_to_string(rv1);
        auto types2 = Parser::type_mask_to_string(rv2);

        if (rv1b == rv2b)
        {
            ss << "WRN: " << types1 << " != " << types2;
            success = true;
        }
        else
        {
            ss << "ERR: " << types1 << " != " << types2;
        }
    }

    report(success, ss.str());

    return success;
}

bool compare_get_operation(const std::optional<std::regex>& check_regex,
                           const Parser& parser1,
                           const GWBUF& copy1,
                           const Parser& parser2,
                           const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_operation         : ";

    sql::OpCode rv1 = parser1.get_operation(copy1);
    sql::OpCode rv2 = parser2.get_operation(copy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        if (!check_regex || std::regex_search(mxs::sql::to_string(rv1), *check_regex))
        {
            ss << "Ok : " << mxs::sql::to_string(rv1);
            success = true;
        }
        else
        {
            ss << "NOT: "
               << mxs::sql::to_string(rv1)
               << " does NOT match regex.";
        }
    }
    else
    {
        ss << "ERR: "
           << mxs::sql::to_string(rv1)
           << " != "
           << mxs::sql::to_string(rv2);
    }

    report(success, ss.str());

    return success;
}

bool compare_get_created_table_name(const Parser& parser1,
                                    const GWBUF& copy1,
                                    const Parser& parser2,
                                    const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_created_table_name: ";

    std::string_view rv1 = parser1.get_created_table_name(copy1);
    std::string_view rv2 = parser2.get_created_table_name(copy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : '" << rv1 << "'";
        success = true;
    }
    else
    {
        ss << "ERR: '" << rv1 << "' != '" << rv2 << "'";
    }

    report(success, ss.str());

    return success;
}

bool compare_get_table_names(const Parser& parser1,
                             const GWBUF& copy1,
                             const Parser& parser2,
                             const GWBUF& copy2)
{
    bool success = false;
    const char* HEADING;

    HEADING = "qc_get_table_names       : ";

    int n1 = 0;
    int n2 = 0;

    std::vector<Parser::TableName> rv1 = parser1.get_table_names(copy1);
    std::vector<Parser::TableName> rv2 = parser2.get_table_names(copy2);

    // The order need not be the same, so let's compare a set.
    std::set<Parser::TableName> names1(rv1.begin(), rv1.end());
    std::set<Parser::TableName> names2(rv2.begin(), rv2.end());

    stringstream ss;
    ss << HEADING;

    if (names1 == names2)
    {
        if (n1 == n2)
        {
            ss << "Ok : ";
            ss << mxb::join(rv1, ", ");
        }
        else
        {
            ss << "WRN: ";
            ss << mxb::join(rv1, ", ");
            ss << " != ";
            ss << mxb::join(rv2, ", ");
        }

        success = true;
    }
    else
    {
        ss << "ERR: ";
        ss << mxb::join(rv1, ", ");
        ss << " != ";
        ss << mxb::join(rv2, ", ");
    }

    report(success, ss.str());

    return success;
}

void add_fields(std::set<string>& m, const char* fields)
{
    const char* begin = fields;
    const char* end = begin;

    // As long as we have not reached the end.
    while (*end != 0)
    {
        // Walk over everything but whitespace.
        while (!isspace(*end) && (*end != 0))
        {
            ++end;
        }

        // Insert whatever we found.
        m.insert(string(begin, end - begin));

        // Walk over all whitespace.
        while (isspace(*end) && (*end != 0))
        {
            ++end;
        }

        // Move begin to the next non-whitespace character.
        begin = end;
    }

    if (begin != end)
    {
        m.insert(string(begin, end - begin));
    }
}

ostream& operator<<(ostream& o, const std::set<string>& s)
{
    std::set<string>::iterator i = s.begin();

    while (i != s.end())
    {
        o << *i;

        ++i;
        if (i != s.end())
        {
            o << " ";
        }
    }

    return o;
}

bool compare_get_database_names(const Parser& parser1,
                                const GWBUF& copy1,
                                const Parser& parser2,
                                const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_database_names    : ";

    std::vector<std::string_view> rv1 = parser1.get_database_names(copy1);
    std::vector<std::string_view> rv2 = parser2.get_database_names(copy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << mxb::join(rv1, ", ");
        success = true;
    }
    else
    {
        ss << "ERR: " << mxb::join(rv1, ", ") << " != " << mxb::join(rv2, ", ");
    }

    report(success, ss.str());

    return success;
}

bool compare_get_prepare_name(const Parser& parser1,
                              const GWBUF& copy1,
                              const Parser& parser2,
                              const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_prepare_name      : ";

    std::string_view rv1 = parser1.get_prepare_name(copy1);
    std::string_view rv2 = parser2.get_prepare_name(copy2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : '" << rv1 << "'";
        success = true;
    }
    else
    {
        ss << "ERR: '" << rv1 << "' != '" << rv2 << "'";
    }

    report(success, ss.str());

    return success;
}

bool operator==(const Parser::FieldInfo& lhs, const Parser::FieldInfo& rhs)
{
    return
        lhs.column == rhs.column
        && lhs.table == rhs.table
        && lhs.database == rhs.database;
}

ostream& operator<<(ostream& out, const Parser::FieldInfo& x)
{
    if (!x.database.empty())
    {
        out << x.database;
        out << ".";
        mxb_assert(!x.table.empty());
    }

    if (!x.table.empty())
    {
        out << x.table;
        out << ".";
    }

    mxb_assert(!x.column.empty());
    out << x.column;

    return out;
}

class QcFieldInfo
{
public:
    QcFieldInfo(const Parser::FieldInfo& info)
        : m_database(info.database)
        , m_table(info.table)
        , m_column(info.column)
        , m_context(info.context)
    {
    }

    bool eq(const QcFieldInfo& rhs) const
    {
        return m_database == rhs.m_database
               && m_table == rhs.m_table
               && m_column == rhs.m_column;
    }

    bool lt(const QcFieldInfo& rhs) const
    {
        bool rv = false;

        if (m_database < rhs.m_database)
        {
            rv = true;
        }
        else if (m_database > rhs.m_database)
        {
            rv = false;
        }
        else
        {
            if (m_table < rhs.m_table)
            {
                rv = true;
            }
            else if (m_table > rhs.m_table)
            {
                rv = false;
            }
            else
            {
                rv = m_column < rhs.m_column;
            }
        }

        return rv;
    }

    bool has_same_name(const QcFieldInfo& o) const
    {
        return m_database == o.m_database
               && m_table == o.m_table
               && m_column == o.m_column;
    }

    void print(ostream& out) const
    {
        if (!m_database.empty())
        {
            out << m_database;
            out << ".";
        }

        if (!m_table.empty())
        {
            out << m_table;
            out << ".";
        }

        out << m_column;

        if (m_context != 0)
        {
            out << "(";
            bool first = true;

            if (m_context & Parser::FIELD_UNION)
            {
                out << (first ? "" : ", ") << "UNION";
                first = false;
            }

            if (m_context & Parser::FIELD_SUBQUERY)
            {
                out << (first ? "" : ", ") << "SUBQUERY";
                first = false;
            }

            out << ")";
        }
    }

private:
    std::string_view m_database;
    std::string_view m_table;
    std::string_view m_column;
    uint32_t         m_context;
};

ostream& operator<<(ostream& out, const QcFieldInfo& x)
{
    x.print(out);
    return out;
}

ostream& operator<<(ostream& out, std::set<QcFieldInfo>& x)
{
    std::set<QcFieldInfo>::iterator i = x.begin();
    std::set<QcFieldInfo>::iterator end = x.end();

    while (i != end)
    {
        out << *i++;
        if (i != end)
        {
            out << " ";
        }
    }

    return out;
}

bool operator<(const QcFieldInfo& lhs, const QcFieldInfo& rhs)
{
    return lhs.lt(rhs);
}

bool operator==(const QcFieldInfo& lhs, const QcFieldInfo& rhs)
{
    return lhs.eq(rhs);
}

bool compare_get_field_info(const Parser& parser1,
                            const GWBUF& copy1,
                            const Parser& parser2,
                            const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_field_info        : ";

    const Parser::FieldInfo* infos1;
    const Parser::FieldInfo* infos2;
    size_t n_infos1;
    size_t n_infos2;

    parser1.get_field_info(copy1, &infos1, &n_infos1);
    parser2.get_field_info(copy2, &infos2, &n_infos2);

    stringstream ss;
    ss << HEADING;

    int i;

    std::set<QcFieldInfo> f1;
    f1.insert(infos1, infos1 + n_infos1);

    std::set<QcFieldInfo> f2;
    f2.insert(infos2, infos2 + n_infos2);

    if (f1 == f2)
    {
        ss << "Ok : ";

        // TODO: Currently qc_sqlite provides context information, while qc_mysqlembedded
        // TODO: does not. To ensure that the output always contains the maximum amount
        // TODO: of information, we simply generate both output and print the longest.

        stringstream ss1;
        ss1 << f1;
        stringstream ss2;
        ss2 << f2;

        ss << (ss1.str().length() > ss2.str().length() ? ss1.str() : ss2.str());
        success = true;
    }
    else
    {
        ss << "ERR: " << f1 << " != " << f2;
    }

    report(success, ss.str());

    return success;
}


class QcFunctionInfo
{
public:
    QcFunctionInfo(const Parser::FunctionInfo& info)
        : m_name(info.name)
        , m_pFields(info.fields)
        , m_nFields(info.n_fields)
    {
        // We want case-insensitive comparisons.
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), tolower);
    }

    bool eq(const QcFunctionInfo& rhs) const
    {
        return m_name == rhs.m_name
               && have_same_fields(*this, rhs);
    }

    bool lt(const QcFunctionInfo& rhs) const
    {
        bool rv = false;

        if (m_name < rhs.m_name)
        {
            rv = true;
        }
        else if (m_name > rhs.m_name)
        {
            rv = false;
        }
        else
        {
            std::set<string> lfs;
            std::set<string> rfs;

            get_fields(&lfs);
            rhs.get_fields(&rfs);

            rv = lfs < rfs;
        }

        return rv;
    }

    const std::string& name() const
    {
        return m_name;
    }

    void print(ostream& out) const
    {
        out << m_name;

        out << "(";

        for (uint32_t i = 0; i < m_nFields; ++i)
        {
            const Parser::FieldInfo& name = m_pFields[i];

            if (!name.database.empty())
            {
                out << name.database << ".";
            }

            if (!name.table.empty())
            {
                out << name.table << ".";
            }

            mxb_assert(!name.column.empty());

            out << name.column;

            if (i < m_nFields - 1)
            {
                out << ", ";
            }
        }

        out << ")";
    }

private:
    void get_fields(std::set<string>* pS) const
    {
        for (size_t i = 0; i < m_nFields; ++i)
        {
            pS->insert(get_field_name(m_pFields[i]));
        }
    }

    static bool have_same_fields(const QcFunctionInfo& lhs, const QcFunctionInfo& rhs)
    {
        bool rv = false;

        if (lhs.m_nFields == rhs.m_nFields)
        {
            std::set<string> lfs;
            lhs.get_fields(&lfs);

            std::set<string> rfs;
            rhs.get_fields(&rfs);

            rv = (lfs == rfs);
        }

        return rv;
    }

    static std::string get_field_name(const Parser::FieldInfo& field)
    {
        string s;

        if (!field.database.empty())
        {
            s += field.database;
            s += ".";
        }

        if (!field.table.empty())
        {
            s += field.table;
            s += ".";
        }

        s += field.column;

        std::transform(s.begin(), s.end(), s.begin(), tolower);

        return s;
    }

private:
    std::string              m_name;
    const Parser::FieldInfo* m_pFields;
    uint32_t                 m_nFields;
};

ostream& operator<<(ostream& out, const QcFunctionInfo& x)
{
    x.print(out);
    return out;
}

ostream& operator<<(ostream& out, std::set<QcFunctionInfo>& x)
{
    std::set<QcFunctionInfo>::iterator i = x.begin();
    std::set<QcFunctionInfo>::iterator end = x.end();

    while (i != end)
    {
        out << *i++;
        if (i != end)
        {
            out << " ";
        }
    }

    return out;
}

bool operator<(const QcFunctionInfo& lhs, const QcFunctionInfo& rhs)
{
    return lhs.lt(rhs);
}

bool operator==(const QcFunctionInfo& lhs, const QcFunctionInfo& rhs)
{
    return lhs.eq(rhs);
}

void collect_missing_function_names(const std::set<QcFunctionInfo>& one,
                                    const std::set<QcFunctionInfo>& other,
                                    std::set<std::string>* pNames)
{
    for (std::set<QcFunctionInfo>::const_iterator i = one.begin(); i != one.end(); ++i)
    {
        if (other.count(*i) == 0)
        {
            pNames->insert(i->name());
        }
    }
}

bool compare_get_function_info(const Parser& parser1,
                               const GWBUF& copy1,
                               const Parser& parser2,
                               const GWBUF& copy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_function_info     : ";

    const Parser::FunctionInfo* infos1;
    const Parser::FunctionInfo* infos2;
    size_t n_infos1;
    size_t n_infos2;

    parser1.get_function_info(copy1, &infos1, &n_infos1);
    parser2.get_function_info(copy2, &infos2, &n_infos2);

    stringstream ss;
    ss << HEADING;

    int i;

    std::set<QcFunctionInfo> f1;
    f1.insert(infos1, infos1 + n_infos1);

    std::set<QcFunctionInfo> f2;
    f2.insert(infos2, infos2 + n_infos2);

    if (f1 == f2)
    {
        ss << "Ok : ";
        ss << f1;
        success = true;
    }
    else
    {
        std::set<std::string> names1;
        collect_missing_function_names(f1, f2, &names1);

        std::set<std::string> names2;
        collect_missing_function_names(f2, f1, &names2);

        // A difference in sizes unconditionally means that there has to be
        // a significant discrepancy.
        bool real_error = (names1.size() != names2.size());

        if (!real_error)
        {
            // We assume that names1 are from the qc_mysqlembedded and names2 from qc_sqlite.
            for (std::set<std::string>::iterator i = names1.begin(); i != names1.end(); ++i)
            {
                if (*i == "date_add_interval")
                {
                    // The embedded parser reports all date_add(), adddate(), date_sub() and subdate()
                    // functions as date_add_interval(). Further, all "DATE + INTERVAL ..." cases become
                    // use of date_add_interval() functions.
                    if ((names2.count("date_add") == 0)
                        && (names2.count("adddate") == 0)
                        && (names2.count("date_sub") == 0)
                        && (names2.count("subdate") == 0)
                        && (names2.count("+") == 0)
                        && (names2.count("-") == 0))
                    {
                        real_error = true;
                    }
                }
                else if (*i == "cast")
                {
                    // The embedded parser returns "convert" as "cast".
                    if (names2.count("convert") == 0)
                    {
                        real_error = true;
                    }
                }
                else if (*i == "substr")
                {
                    // The embedded parser returns "substring" as "substr".
                    if (names2.count("substring") == 0)
                    {
                        real_error = true;
                    }
                }
                else
                {
                    real_error = true;
                }
            }
        }

        if (real_error)
        {
            ss << "ERR: " << f1 << " != " << f2;
        }
        else
        {
            ss << "Ok : " << f1 << " != " << f2;
            success = true;
        }
    }

    report(success, ss.str());

    return success;
}

namespace
{

bool specified(const set<string>& properties, const string& key)
{
    return properties.empty() || properties.count(key) != 0;
}

}

bool compare(const set<string>& properties,
             const std::optional<std::regex>& check_regex,
             const Parser& parser1,
             const GWBUF& copy1,
             const Parser& parser2,
             const GWBUF& copy2)
{
    int errors = 0;

    errors += !compare_parse(parser1, copy1, parser2, copy2);

    if (specified(properties, "type"))
    {
        errors += !compare_get_type(check_regex, parser1, copy1, parser2, copy2);
    }

    if (specified(properties, "operation"))
    {
        errors += !compare_get_operation(check_regex, parser1, copy1, parser2, copy2);
    }

    if (specified(properties, "created_table_name"))
    {
        errors += !compare_get_created_table_name(parser1, copy1, parser2, copy2);
    }

    if (specified(properties, "table_names"))
    {
        errors += !compare_get_table_names(parser1, copy1, parser2, copy2);
    }

    if (specified(properties, "database_names"))
    {
        errors += !compare_get_database_names(parser1, copy1, parser2, copy2);
    }

    if (specified(properties, "prepare_name"))
    {
        errors += !compare_get_prepare_name(parser1, copy1, parser2, copy2);
    }

    if (specified(properties, "field_info"))
    {
        errors += !compare_get_field_info(parser1, copy1, parser2, copy2);
    }

    if (specified(properties, "function_info"))
    {
        errors += !compare_get_function_info(parser1, copy1, parser2, copy2);
    }

    if (global.result_printed)
    {
        cout << endl;
    }

    bool success = (errors == 0);

    uint32_t type_mask1 = parser1.get_type_mask(copy1);
    uint32_t type_mask2 = parser2.get_type_mask(copy2);

    if ((type_mask1 == type_mask2)
        && ((type_mask1 & sql::TYPE_PREPARE_NAMED_STMT) || (type_mask1 & sql::TYPE_PREPARE_STMT)))
    {
        GWBUF* pPreparable1 = parser1.get_preparable_stmt(copy1);
        GWBUF* pPreparable2 = parser2.get_preparable_stmt(copy2);

        if (pPreparable1 && pPreparable2)
        {
            string indent = global.indent;
            global.indent += string(4, ' ');

            success = compare(properties, check_regex, parser1, *pPreparable1, parser2, *pPreparable2);

            global.indent = indent;
        }
    }

    return success;
}

bool compare(const set<string>& properties,
             const std::optional<std::regex>& check_regex,
             Parser& parser1, Parser& parser2, const string& s)
{
    GWBUF copy1 = parser1.helper().create_packet(s);
    GWBUF copy2 = parser2.helper().create_packet(s);

    bool success = compare(properties, check_regex, parser1, copy1, parser2, copy2);

    if (success)
    {
        SetSqlModeParser::sql_mode_t sql_mode;
        SetSqlModeParser parser;

        std::string_view sql = parser1.get_sql(copy1);
        if (parser.get_sql_mode(sql, &sql_mode) == SetSqlModeParser::IS_SET_SQL_MODE)
        {
            switch (sql_mode)
            {
            case SetSqlModeParser::DEFAULT:
                parser1.set_sql_mode(Parser::SqlMode::DEFAULT);
                parser2.set_sql_mode(Parser::SqlMode::DEFAULT);
                break;

            case SetSqlModeParser::ORACLE:
                parser1.set_sql_mode(Parser::SqlMode::ORACLE);
                parser2.set_sql_mode(Parser::SqlMode::ORACLE);
                break;

            default:
                mxb_assert(!true);

            case SetSqlModeParser::SOMETHING:
                break;
            }
        }
    }

    return success;
}

inline void ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
}

inline void rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(),
                         s.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))).base(),
            s.end());
}

static void trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}

int run(mxs::TestReader::Expect expect,
        const set<string>& properties,
        const std::optional<std::regex>& test_regex,
        const std::optional<std::regex>& check_regex,
        Parser& parser1,
        Parser& parser2,
        istream& in)
{
    bool stop = false;      // Whether we should exit.

    mxs::TestReader reader(expect, in);

    while (!stop && (reader.get_statement(global.query) == mxs::TestReader::RESULT_STMT))
    {
        if (!test_regex || std::regex_search(global.query, *test_regex))
        {
            global.line = reader.line();
            global.query_printed = false;
            global.result_printed = false;

            ++global.n_statements;

            if (global.verbosity >= VERBOSITY_EXTENDED)
            {
                // In case the execution crashes, we want the query printed.
                report_query();
            }

            bool success = compare(properties, check_regex, parser1, parser2, global.query);

            if (!success)
            {
                ++global.n_errors;

                if (global.stop_at_error)
                {
                    stop = true;
                }
            }

            global.query.clear();
        }
    }

    return global.n_errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int run(const set<string>& properties,
        const std::optional<std::regex>& test_regex,
        const std::optional<std::regex>& check_regex,
        Parser& parser1, Parser& parser2, const string& statement)
{
    if (!test_regex || std::regex_search(statement, *test_regex))
    {
        global.query = statement;

        ++global.n_statements;

        if (global.verbosity >= VERBOSITY_EXTENDED)
        {
            // In case the execution crashes, we want the query printed.
            report_query();
        }

        if (!compare(properties, check_regex, parser1, parser2, global.query))
        {
            ++global.n_errors;
        }
    }

    return global.n_errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

void append_arg(string& args, const string& arg)
{
    if (!args.empty())
    {
        args += ",";
    }
    args += arg;
}
}

int main(int argc, char* argv[])
{
    int rc = EXIT_SUCCESS;

    const char* zClassifier1 = "qc_mysqlembedded";
    const char* zClassifier2 = "qc_sqlite";
    string classifier1Args;
    string classifier2Args("log_unrecognized_statements=1");
    string statement;
    const char* zStatement = NULL;
    Parser::SqlMode sql_mode = Parser::SqlMode::DEFAULT;
    std::optional<std::regex> test_regex;
    std::optional<std::regex> check_regex;
    const Parser::Helper* pHelper = &MariaDBParser::Helper::get();
    bool solo = false;
    mxs::TestReader::Expect expect = mxs::TestReader::Expect::MARIADB;
    set<string> properties;

    size_t rounds = 1;
    int v = VERBOSITY_NORMAL;
    int c;
    while ((c = getopt(argc, argv, "r:d0:1:2:v:A:B:C:m:x:c:s:SRH:p:")) != -1)
    {
        switch (c)
        {
        case 'r':
            rounds = atoi(optarg);
            break;

        case 'v':
            v = atoi(optarg);
            break;

        case '0':
            zClassifier1 = optarg;
            zClassifier2 = nullptr;
            solo = true;
            break;

        case '1':
            zClassifier1 = optarg;
            break;

        case '2':
            zClassifier2 = optarg;
            break;

        case 'A':
            append_arg(classifier1Args, optarg);
            break;

        case 'B':
            append_arg(classifier2Args, optarg);
            break;

        case 'C':
            append_arg(classifier1Args, optarg);
            append_arg(classifier2Args, optarg);
            break;

        case 'd':
            global.stop_at_error = false;
            break;

        case 's':
            {
                const char* z = optarg;

                while (*z)
                {
                    switch (*z)
                    {
                    case '\\':
                        if (*(z + 1) == 'n')
                        {
                            statement += '\n';
                            ++z;
                        }
                        else
                        {
                            statement += *z;
                        }
                        break;

                    default:
                        statement += *z;
                    }

                    ++z;
                }

                zStatement = statement.c_str();
            }
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
                rc = EXIT_FAILURE;
                break;
            }
            break;

        case 'x':
            {
                auto flags = std::regex_constants::ECMAScript | std::regex_constants::icase;
                test_regex = std::regex(optarg, flags);
            }
            break;

        case 'c':
            {
                auto flags = std::regex_constants::ECMAScript | std::regex_constants::icase;
                check_regex = std::regex(optarg, flags);
            }
            break;

        case 'S':
            global.strict = true;
            break;

        case 'R':
            global.strict_reporting = true;
            break;

        case 'H':
            if (strcmp(optarg, "mariadb") == 0)
            {
                pHelper = &MariaDBParser::Helper::get();
            }
            else if(strcmp(optarg, "postgres") == 0)
            {
                pHelper = &PgParser::Helper::get();
                expect = mxs::TestReader::Expect::POSTGRES;
            }
            else
            {
                rc = EXIT_FAILURE;
                break;
            }
            break;

        case 'p':
            {
                auto tokens = mxb::strtok(optarg, "|");
                properties.insert(tokens.begin(), tokens.end());
            }
            break;

        default:
            rc = EXIT_FAILURE;
            break;
        }
    }

    if ((rc == EXIT_SUCCESS) && (v >= VERBOSITY_MIN && v <= VERBOSITY_MAX))
    {
        rc = EXIT_FAILURE;
        global.verbosity = static_cast<verbosity_t>(v);

        int n = argc - (optind - 1);

        if (n >= 1)
        {
            mxs::set_datadir("/tmp");
            mxs::set_langdir(".");
            mxs::set_process_datadir("/tmp");

            if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
            {
                const char* zClassifier1Args = classifier1Args.c_str();
                const char* zClassifier2Args = classifier2Args.c_str();

                ParserPlugin* pPlugin1;
                ParserPlugin* pPlugin2;

                if (get_plugins(sql_mode,
                                zClassifier1,
                                zClassifier1Args,
                                &pPlugin1,
                                zClassifier2,
                                zClassifier2Args,
                                &pPlugin2))
                {
                    size_t round = 0;
                    bool terminate = false;

                    if (solo)
                    {
                        pPlugin2 = pPlugin1;
                    }

                    std::unique_ptr<Parser> sParser1 = pPlugin1->create_parser(pHelper);
                    std::unique_ptr<Parser> sParser2 = pPlugin2->create_parser(pHelper);

                    do
                    {
                        ++round;

                        global.n_statements = 0;
                        global.n_errors = 0;
                        global.query_printed = false;
                        global.result_printed = false;

                        if (zStatement)
                        {
                            rc = run(properties, test_regex, check_regex, *sParser1, *sParser2, zStatement);
                        }
                        else if (n == 1)
                        {
                            rc = run(expect, properties, test_regex, check_regex, *sParser1, *sParser2, cin);
                        }
                        else
                        {
                            for (int i = optind; i < argc; ++i)
                            {
                                cout << argv[i] << endl;

                                ifstream in(argv[i]);

                                if (in)
                                {
                                    rc = run(expect, properties, test_regex, check_regex,
                                             *sParser1, *sParser2, in);
                                }
                                else
                                {
                                    terminate = true;
                                    cerr << "error: Could not open " << argv[argc - 1] << "." << endl;
                                }
                            }
                        }

                        cout << "\n"
                             << "Statements: " << global.n_statements << endl
                             << "Errors    : " << global.n_errors << endl;

                        if (!terminate && ((rounds == 0) || (round < rounds)))
                        {
                            cout << endl;
                        }
                    }
                    while (!terminate && ((rounds == 0) || (round < rounds)));

                    if (solo)
                    {
                        pPlugin2 = nullptr;
                    }

                    put_plugins(pPlugin1, pPlugin2);

                    cout << "\n";
                    cout << "1st classifier: "
                         << global.time1.tv_sec << "."
                         << global.time1.tv_nsec
                         << endl;
                    cout << "2nd classifier: "
                         << global.time2.tv_sec << "."
                         << global.time2.tv_nsec
                         << endl;

                    sParser1.reset();
                    sParser2.reset();

                    unload_all_modules();
                }

                mxs_log_finish();
            }
            else
            {
                cerr << "error: Could not initialize log." << endl;
            }
        }
        else
        {
            cout << USAGE << endl;
        }
    }
    else
    {
        cout << USAGE << endl;
    }

    return rc;
}
