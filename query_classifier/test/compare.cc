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

#include <unistd.h>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <my_config.h>
#include <maxscale/paths.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/query_classifier.h>
#include "../../server/modules/protocol/MySQL/mariadbclient/setsqlmodeparser.hh"
#include "testreader.hh"
using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::ifstream;
using std::istream;
using std::map;
using std::ostream;
using std::string;
using std::stringstream;

#if MYSQL_VERSION_MAJOR == 10 && MYSQL_VERSION_MINOR == 3
#define USING_MARIADB_103
#else
#undef USING_MARIADB_103
#endif

namespace
{

char USAGE[] =
    "usage: compare [-r count] [-d] [-1 classfier1] [-2 classifier2] "
        "[-A args] [-B args] [-C args] [-m [default|oracle]] [-v [0..2]] [-s statement]|[file]]\n\n"
    "-r    redo the test the specified number of times; 0 means forever, default is 1\n"
    "-d    don't stop after first failed query\n"
    "-1    the first classifier, default 'qc_mysqlembedded'\n"
    "-2    the second classifier, default 'qc_sqlite'\n"
    "-A    arguments for the first classifier\n"
    "-B    arguments for the second classifier\n"
    "-C    arguments for both classifiers\n"
    "-m    initial sql mode, 'default' or 'oracle', default is 'default'\n"
    "-s    compare single statement\n"
    "-S    strict, also require that the parse result is identical\n"
    "-R    strict reporting, report if parse result is different\n"
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
    bool query_printed;
    string query;
    verbosity_t verbosity;
    bool result_printed;
    bool stop_at_error;
    bool strict;
    bool strict_reporting;
    size_t line;
    size_t n_statements;
    size_t n_errors;
    struct timespec time1;
    struct timespec time2;
    string indent;
} global = { false,            // query_printed
             "",               // query
             VERBOSITY_NORMAL, // verbosity
             false,            // result_printed
             true,             // stop_at_error
             false,            // strict
             false,            // strict reporting
             0,                // line
             0,                // n_statements
             0,                // n_errors
             { 0, 0 },         // time1
             { 0,  0},         // time2
             ""                // indent
};

ostream& operator << (ostream& out, qc_parse_result_t x)
{
    switch (x)
    {
    case QC_QUERY_INVALID:
        out << "QC_QUERY_INVALID";
        break;

    case QC_QUERY_TOKENIZED:
        out << "QC_QUERY_TOKENIZED";
        break;

    case QC_QUERY_PARTIALLY_PARSED:
        out << "QC_QUERY_PARTIALLY_PARSED";
        break;

    case QC_QUERY_PARSED:
        out << "QC_QUERY_PARSED";
        break;

    default:
        out << "static_cast<c_parse_result_t>(" << (int)x << ")";
    }

    return out;
}

GWBUF* create_gwbuf(const string& s)
{
    size_t len = s.length();
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* gwbuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(gwbuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(gwbuf) + 5, s.c_str(), len);

    return gwbuf;
}

QUERY_CLASSIFIER* load_classifier(const char* name)
{
    bool loaded = false;
    size_t len = strlen(name);
    char libdir[len + 1];

    sprintf(libdir, "../%s", name);

    set_libdir(strdup(libdir));

    QUERY_CLASSIFIER *pClassifier = qc_load(name);

    if (!pClassifier)
    {
        cerr << "error: Could not load classifier " << name << "." << endl;
    }

    return pClassifier;
}

QUERY_CLASSIFIER* get_classifier(const char* zName, qc_sql_mode_t sql_mode, const char* zArgs)
{
    QUERY_CLASSIFIER* pClassifier = load_classifier(zName);

    if (pClassifier)
    {
        if (pClassifier->qc_setup(sql_mode, zArgs) != QC_RESULT_OK ||
            pClassifier->qc_process_init() != QC_RESULT_OK ||
            pClassifier->qc_thread_init() != QC_RESULT_OK)
        {
            cerr << "error: Could not setup or init classifier " << zName << "." << endl;
            qc_unload(pClassifier);
            pClassifier = 0;
        }
    }

    return pClassifier;
}

void put_classifier(QUERY_CLASSIFIER* pClassifier)
{
    if (pClassifier)
    {
        pClassifier->qc_process_end();
        qc_unload(pClassifier);
    }
}

bool get_classifiers(qc_sql_mode_t sql_mode,
                     const char* zName1, const char* zArgs1, QUERY_CLASSIFIER** ppClassifier1,
                     const char* zName2, const char* zArgs2, QUERY_CLASSIFIER** ppClassifier2)
{
    bool rc = false;

    QUERY_CLASSIFIER* pClassifier1 = get_classifier(zName1, sql_mode, zArgs1);

    if (pClassifier1)
    {
        QUERY_CLASSIFIER* pClassifier2 = get_classifier(zName2, sql_mode, zArgs2);

        if (pClassifier2)
        {
            *ppClassifier1 = pClassifier1;
            *ppClassifier2 = pClassifier2;
            rc = true;
        }
        else
        {
            put_classifier(pClassifier1);
        }
    }

    return rc;
}

void put_classifiers(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2)
{
    put_classifier(pClassifier1);
    put_classifier(pClassifier2);
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
    timespec result = { 0, 0 };

    ss_dassert((later.tv_sec > earlier.tv_sec) ||
               ((later.tv_sec == earlier.tv_sec) && (later.tv_nsec > earlier.tv_nsec)));

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

bool compare_parse(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                   QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_parse                 : ";

    struct timespec start;
    struct timespec finish;

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    int32_t rv1;
    pClassifier1->qc_parse(pCopy1, QC_COLLECT_ESSENTIALS, &rv1);
    clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
    update_time(&global.time1, start, finish);

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    int32_t rv2;
    pClassifier2->qc_parse(pCopy2, QC_COLLECT_ESSENTIALS, &rv2);
    clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
    update_time(&global.time2, start, finish);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << static_cast<qc_parse_result_t>(rv1);
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

        ss << static_cast<qc_parse_result_t>(rv1) << " != " << static_cast<qc_parse_result_t>(rv2);
    }

    report(success, ss.str());

    return success;
}

bool compare_get_type(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                      QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_type_mask         : ";

    uint32_t rv1;
    pClassifier1->qc_get_type_mask(pCopy1, &rv1);
    uint32_t rv2;
    pClassifier2->qc_get_type_mask(pCopy2, &rv2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        char* types = qc_typemask_to_string(rv1);
        ss << "Ok : " << types;
        free(types);
        success = true;
    }
    else
    {
        uint32_t rv1b = rv1;

        if (rv1b & QUERY_TYPE_WRITE)
        {
            rv1b &= ~(uint32_t)QUERY_TYPE_READ;
        }

        uint32_t rv2b = rv2;

        if (rv2b & QUERY_TYPE_WRITE)
        {
            rv2b &= ~(uint32_t)QUERY_TYPE_READ;
        }

        if (rv1b & QUERY_TYPE_READ)
        {
            rv1b &= ~(uint32_t)QUERY_TYPE_LOCAL_READ;
        }

        if (rv2b & QUERY_TYPE_READ)
        {
            rv2b &= ~(uint32_t)QUERY_TYPE_LOCAL_READ;
        }

        char* types1 = qc_typemask_to_string(rv1);
        char* types2 = qc_typemask_to_string(rv2);

        if (rv1b == rv2b)
        {
            ss << "WRN: " << types1 << " != " << types2;
            success = true;
        }
        else
        {
            ss << "ERR: " << types1 << " != " << types2;
        }
        free(types1);
        free(types2);
    }

    report(success, ss.str());

    return success;
}

bool compare_get_operation(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                           QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_operation         : ";

    int32_t rv1;
    pClassifier1->qc_get_operation(pCopy1, &rv1);
    int32_t rv2;
    pClassifier2->qc_get_operation(pCopy2, &rv2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << qc_op_to_string(static_cast<qc_query_op_t>(rv1));
        success = true;
    }
    else
    {
        ss << "ERR: "
           << qc_op_to_string(static_cast<qc_query_op_t>(rv1))
           << " != "
           << qc_op_to_string(static_cast<qc_query_op_t>(rv2));
    }

    report(success, ss.str());

    return success;
}

bool compare_get_created_table_name(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                    QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_created_table_name: ";

    char* rv1;
    pClassifier1->qc_get_created_table_name(pCopy1, &rv1);
    char* rv2;
    pClassifier2->qc_get_created_table_name(pCopy2, &rv2);

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || (rv1 && rv2 && (strcmp(rv1, rv2) == 0)))
    {
        ss << "Ok : " << (rv1 ? rv1 : "NULL");
        success = true;
    }
    else
    {
        ss << "ERR: " << (rv1 ? rv1 : "NULL") << " != " << (rv2 ? rv2 : "NULL");
    }

    report(success, ss.str());

    free(rv1);
    free(rv2);

    return success;
}

bool compare_is_drop_table_query(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                 QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_is_drop_table_query   : ";

    int32_t rv1;
    pClassifier1->qc_is_drop_table_query(pCopy1, &rv1);
    int32_t rv2;
    pClassifier2->qc_is_drop_table_query(pCopy2, &rv2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << static_cast<bool>(rv1);
        success = true;
    }
    else
    {
        ss << "ERR: " << static_cast<bool>(rv1) << " != " << static_cast<bool>(rv2);
    }

    report(success, ss.str());

    return success;
}

bool compare_strings(const char* const* strings1, const char* const* strings2, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const char* s1 = strings1[i];
        const char* s2 = strings2[i];

        if (strcmp(s1, s2) != 0)
        {
            return false;
        }
    }

    return true;
}

void free_strings(char** strings, int n)
{
    if (strings)
    {
        for (int i = 0; i < n; ++i)
        {
            free(strings[i]);
        }

        free(strings);
    }
}

void print_names(ostream& out, const char* const* strings, int n)
{
    if (strings)
    {
        for (int i = 0; i < n; ++i)
        {
            out << strings[i];
            if (i < n - 1)
            {
                out << ", ";
            }
        }
    }
    else
    {
        out << "NULL";
    }
}

bool compare_get_table_names(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                             QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2,
                             bool full)
{
    bool success = false;
    const char* HEADING;

    if (full)
    {
        HEADING = "qc_get_table_names(full) : ";
    }
    else
    {
        HEADING = "qc_get_table_names       : ";
    }

    int n1 = 0;
    int n2 = 0;

    char** rv1;
    pClassifier1->qc_get_table_names(pCopy1, full, &rv1, &n1);
    char** rv2;
    pClassifier2->qc_get_table_names(pCopy2, full, &rv2, &n2);

    // The order need not be the same, so let's compare a set.
    std::set<string> names1;
    std::set<string> names2;

    if (rv1)
    {
        std::copy(rv1, rv1 + n1, inserter(names1, names1.begin()));
    }

    if (rv2)
    {
        std::copy(rv2, rv2 + n2, inserter(names2, names2.begin()));
    }

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || (names1 == names2))
    {
        if (n1 == n2)
        {
            ss << "Ok : ";
            print_names(ss, rv1, n1);
        }
        else
        {
            ss << "WRN: ";
            print_names(ss, rv1, n1);
            ss << " != ";
            print_names(ss, rv2, n2);
        }

        success = true;
    }
    else
    {
        ss << "ERR: ";
        print_names(ss, rv1, n1);
        ss << " != ";
        print_names(ss, rv2, n2);
    }

    report(success, ss.str());

    free_strings(rv1, n1);
    free_strings(rv2, n2);

    return success;
}

bool compare_query_has_clause(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                              QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_query_has_clause      : ";

    int32_t rv1;
    pClassifier1->qc_query_has_clause(pCopy1, &rv1);
    int32_t rv2;
    pClassifier2->qc_query_has_clause(pCopy2, &rv2);

    stringstream ss;
    ss << HEADING;

    if (rv1 == rv2)
    {
        ss << "Ok : " << static_cast<bool>(rv1);
        success = true;
    }
    else
    {
        ss << "ERR: " << static_cast<bool>(rv1) << " != " << static_cast<bool>(rv2);
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

ostream& operator << (ostream& o, const std::set<string>& s)
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

bool compare_get_database_names(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_database_names    : ";

    int n1 = 0;
    int n2 = 0;

    char** rv1;
    pClassifier1->qc_get_database_names(pCopy1, &rv1, &n1);
    char** rv2;
    pClassifier2->qc_get_database_names(pCopy2, &rv2, &n2);

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || ((n1 == n2) && compare_strings(rv1, rv2, n1)))
    {
        ss << "Ok : ";
        print_names(ss, rv1, n1);
        success = true;
    }
    else
    {
        ss << "ERR: ";
        print_names(ss, rv1, n1);
        ss << " != ";
        print_names(ss, rv2, n2);
    }

    report(success, ss.str());

    free_strings(rv1, n1);
    free_strings(rv2, n2);

    return success;
}

bool compare_get_prepare_name(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                              QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_prepare_name      : ";

    char* rv1;
    pClassifier1->qc_get_prepare_name(pCopy1, &rv1);
    char* rv2;
    pClassifier2->qc_get_prepare_name(pCopy2, &rv2);

    stringstream ss;
    ss << HEADING;

    if ((!rv1 && !rv2) || (rv1 && rv2 && (strcmp(rv1, rv2) == 0)))
    {
        ss << "Ok : " << (rv1 ? rv1 : "NULL");
        success = true;
    }
    else
    {
        ss << "ERR: " << (rv1 ? rv1 : "NULL") << " != " << (rv2 ? rv2 : "NULL");
    }

    report(success, ss.str());

    free(rv1);
    free(rv2);

    return success;
}

bool operator == (const QC_FIELD_INFO& lhs, const QC_FIELD_INFO& rhs)
{
    bool rv = false;
    if (lhs.column && rhs.column && (strcasecmp(lhs.column, rhs.column) == 0))
    {
        if (!lhs.table && !rhs.table)
        {
            rv = true;
        }
        else if (lhs.table && rhs.table && (strcmp(lhs.table, rhs.table) == 0))
        {
            if (!lhs.database && !rhs.database)
            {
                rv = true;
            }
            else if (lhs.database && rhs.database && (strcmp(lhs.database, rhs.database) == 0))
            {
                rv = true;
            }
        }
    }

    return rv;
}

ostream& operator << (ostream& out, const QC_FIELD_INFO& x)
{
    if (x.database)
    {
        out << x.database;
        out << ".";
        ss_dassert(x.table);
    }

    if (x.table)
    {
        out << x.table;
        out << ".";
    }

    ss_dassert(x.column);
    out << x.column;

    return out;
}

class QcFieldInfo
{
public:
    QcFieldInfo(const QC_FIELD_INFO& info)
        : m_database(info.database ? info.database : "")
        , m_table(info.table ? info.table : "")
        , m_column(info.column ? info.column : "")
    {}

    bool eq(const QcFieldInfo& rhs) const
    {
        return
            m_database == rhs.m_database &&
            m_table == rhs.m_table &&
            m_column == rhs.m_column;
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
        return
            m_database == o.m_database &&
            m_table == o.m_table &&
            m_column == o.m_column;
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
    }

private:
    std::string m_database;
    std::string m_table;
    std::string m_column;
};

ostream& operator << (ostream& out, const QcFieldInfo& x)
{
    x.print(out);
    return out;
}

ostream& operator << (ostream& out, std::set<QcFieldInfo>& x)
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

bool operator < (const QcFieldInfo& lhs, const QcFieldInfo& rhs)
{
    return lhs.lt(rhs);
}

bool operator == (const QcFieldInfo& lhs, const QcFieldInfo& rhs)
{
    return lhs.eq(rhs);
}

bool compare_get_field_info(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                            QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_field_info        : ";

    const QC_FIELD_INFO* infos1;
    const QC_FIELD_INFO* infos2;
    uint32_t n_infos1;
    uint32_t n_infos2;

    pClassifier1->qc_get_field_info(pCopy1, &infos1, &n_infos1);
    pClassifier2->qc_get_field_info(pCopy2, &infos2, &n_infos2);

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
        ss << f1;
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
    QcFunctionInfo(const QC_FUNCTION_INFO& info)
        : m_name(info.name)
        , m_pFields(info.fields)
        , m_nFields(info.n_fields)
    {
        // We want case-insensitive comparisons.
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), tolower);
    }

    bool eq(const QcFunctionInfo& rhs) const
    {
        return
            m_name == rhs.m_name &&
            have_same_fields(*this, rhs);
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
            const QC_FIELD_INFO& name = m_pFields[i];

            if (name.database)
            {
                out << name.database << ".";
            }

            if (name.table)
            {
                out << name.table << ".";
            }

            ss_dassert(name.column);

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

    static std::string get_field_name(const QC_FIELD_INFO& field)
    {
        string s;

        if (field.database)
        {
            s += field.database;
            s += ".";
        }

        if (field.table)
        {
            s += field.table;
            s += ".";
        }

        s += field.column;

        std::transform(s.begin(), s.end(), s.begin(), tolower);

        return s;
    }

private:
    std::string          m_name;
    const QC_FIELD_INFO* m_pFields;
    uint32_t             m_nFields;
};

ostream& operator << (ostream& out, const QcFunctionInfo& x)
{
    x.print(out);
    return out;
}

ostream& operator << (ostream& out, std::set<QcFunctionInfo>& x)
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

bool operator < (const QcFunctionInfo& lhs, const QcFunctionInfo& rhs)
{
    return lhs.lt(rhs);
}

bool operator == (const QcFunctionInfo& lhs, const QcFunctionInfo& rhs)
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

bool compare_get_function_info(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                               QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    bool success = false;
    const char HEADING[] = "qc_get_function_info     : ";

    const QC_FUNCTION_INFO* infos1;
    const QC_FUNCTION_INFO* infos2;
    uint32_t n_infos1;
    uint32_t n_infos2;

    pClassifier1->qc_get_function_info(pCopy1, &infos1, &n_infos1);
    pClassifier2->qc_get_function_info(pCopy2, &infos2, &n_infos2);

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

        bool real_error = false;

        // We assume that names1 are from the qc_mysqlembedded and names2 from qc_sqlite.
        // The embedded parser reports all date_add(), adddate(), date_sub() and subdate()
        // functions as date_add_interval(). Further, all "DATE + INTERVAL ..." cases become
        // use of date_add_interval() functions.
        for  (std::set<std::string>::iterator i = names1.begin(); i != names1.end(); ++i)
        {
            if (*i == "date_add_interval")
            {
                if ((names2.count("date_add") == 0) &&
                    (names2.count("adddate") == 0) &&
                    (names2.count("date_sub") == 0) &&
                    (names2.count("subdate") == 0) &&
                    (names2.count("+") == 0) &&
                    (names2.count("-") == 0))
                {
                    real_error = true;
                }
            }
            else
            {
                real_error = true;
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


bool compare(QUERY_CLASSIFIER* pClassifier1, GWBUF* pBuf1,
             QUERY_CLASSIFIER* pClassifier2, GWBUF* pBuf2)
{
    int errors = 0;

    errors += !compare_parse(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_type(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_operation(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_created_table_name(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_is_drop_table_query(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_table_names(pClassifier1, pBuf1, pClassifier2, pBuf2, false);
    errors += !compare_get_table_names(pClassifier1, pBuf1, pClassifier2, pBuf2, true);
    errors += !compare_query_has_clause(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_database_names(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_prepare_name(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_field_info(pClassifier1, pBuf1, pClassifier2, pBuf2);
    errors += !compare_get_function_info(pClassifier1, pBuf1, pClassifier2, pBuf2);

    if (global.result_printed)
    {
        cout << endl;
    }

    bool success = (errors == 0);

    uint32_t type_mask1;
    pClassifier1->qc_get_type_mask(pBuf1, &type_mask1);

    uint32_t type_mask2;
    pClassifier2->qc_get_type_mask(pBuf2, &type_mask2);

    if ((type_mask1 == type_mask2) &&
        ((type_mask1 & QUERY_TYPE_PREPARE_NAMED_STMT) || (type_mask1 & QUERY_TYPE_PREPARE_STMT)))
    {
        GWBUF* pPreparable1;
        pClassifier1->qc_get_preparable_stmt(pBuf1, &pPreparable1);
        ss_dassert(pPreparable1);

        GWBUF* pPreparable2;
        pClassifier2->qc_get_preparable_stmt(pBuf2, &pPreparable2);
        ss_dassert(pPreparable2);

        string indent = global.indent;
        global.indent += string(4, ' ');

        success = compare(pClassifier1, pPreparable1,
                          pClassifier2, pPreparable2);

        global.indent = indent;
    }

    return success;
}

bool compare(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2, const string& s)
{
    GWBUF* pCopy1 = create_gwbuf(s);
    GWBUF* pCopy2 = create_gwbuf(s);

    bool success = compare(pClassifier1, pCopy1, pClassifier2, pCopy2);

    if (success)
    {
        SetSqlModeParser::sql_mode_t sql_mode;
        SetSqlModeParser parser;

        if (parser.get_sql_mode(&pCopy1, &sql_mode) == SetSqlModeParser::IS_SET_SQL_MODE)
        {
            switch (sql_mode)
            {
            case SetSqlModeParser::DEFAULT:
                pClassifier1->qc_set_sql_mode(QC_SQL_MODE_DEFAULT);
                pClassifier2->qc_set_sql_mode(QC_SQL_MODE_DEFAULT);
                break;

            case SetSqlModeParser::ORACLE:
                pClassifier1->qc_set_sql_mode(QC_SQL_MODE_ORACLE);
                pClassifier2->qc_set_sql_mode(QC_SQL_MODE_ORACLE);
                break;

            default:
                ss_dassert(!true);
            case SetSqlModeParser::SOMETHING:
                break;
            };
        }
    }

    gwbuf_free(pCopy1);
    gwbuf_free(pCopy2);

    return success;
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

static void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

int run(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2, istream& in)
{
    bool stop = false; // Whether we should exit.

    maxscale::TestReader reader(in);

    while (!stop && (reader.get_statement(global.query) == maxscale::TestReader::RESULT_STMT))
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

        bool success = compare(pClassifier1, pClassifier2, global.query);

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

    return global.n_errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int run(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2, const string& statement)
{
    global.query = statement;

    ++global.n_statements;

    if (global.verbosity >= VERBOSITY_EXTENDED)
    {
        // In case the execution crashes, we want the query printed.
        report_query();
    }

    if (!compare(pClassifier1, pClassifier2, global.query))
    {
        ++global.n_errors;
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
    uint64_t version;
#if defined(USING_MARIADB_103)
    string classifier2Args("parse_as=10.3,log_unrecognized_statements=1");
    version = 10 * 1000 * 3 * 100;
#else
    string classifier2Args("log_unrecognized_statements=1");
    version = 10 * 1000 * 2 * 100;
#endif
    const char* zStatement = NULL;
    qc_sql_mode_t sql_mode = QC_SQL_MODE_DEFAULT;

    size_t rounds = 1;
    int v = VERBOSITY_NORMAL;
    int c;
    while ((c = getopt(argc, argv, "r:d1:2:v:A:B:C:m:s:SR")) != -1)
    {
        switch (c)
        {
        case 'r':
            rounds = atoi(optarg);
            break;

        case 'v':
            v = atoi(optarg);
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
            zStatement = optarg;
            break;

        case 'm':
            if (strcasecmp(optarg, "default") == 0)
            {
                sql_mode = QC_SQL_MODE_DEFAULT;
            }
            else if (strcasecmp(optarg, "oracle") == 0)
            {
                sql_mode = QC_SQL_MODE_ORACLE;
            }
            else
            {
                rc = EXIT_FAILURE;
                break;
            }
            break;

        case 'S':
            global.strict = true;
            break;

        case 'R':
            global.strict_reporting = true;
            break;

        default:
            rc = EXIT_FAILURE;
            break;
        };
    }

    if ((rc == EXIT_SUCCESS) && (v >= VERBOSITY_MIN && v <= VERBOSITY_MAX))
    {
        rc = EXIT_FAILURE;
        global.verbosity = static_cast<verbosity_t>(v);

        int n = argc - (optind - 1);

        if ((n == 1) || (n == 2))
        {
            set_datadir(strdup("/tmp"));
            set_langdir(strdup("."));
            set_process_datadir(strdup("/tmp"));

            if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
            {
                const char* zClassifier1Args = classifier1Args.c_str();
                const char* zClassifier2Args = classifier2Args.c_str();

                QUERY_CLASSIFIER* pClassifier1;
                QUERY_CLASSIFIER* pClassifier2;

                if (get_classifiers(sql_mode,
                                    zClassifier1, zClassifier1Args, &pClassifier1,
                                    zClassifier2, zClassifier2Args, &pClassifier2))
                {
                    size_t round = 0;
                    bool terminate = false;

                    pClassifier1->qc_set_server_version(version);
                    pClassifier2->qc_set_server_version(version);

                    do
                    {
                        ++round;

                        global.n_statements = 0;
                        global.n_errors = 0;
                        global.query_printed = false;
                        global.result_printed = false;

                        if (zStatement)
                        {
                            rc = run(pClassifier1, pClassifier2, zStatement);
                        }
                        else if (n == 1)
                        {
                            rc = run(pClassifier1, pClassifier2, cin);
                        }
                        else
                        {
                            ss_dassert(n == 2);

                            ifstream in(argv[argc - 1]);

                            if (in)
                            {
                                rc = run(pClassifier1, pClassifier2, in);
                            }
                            else
                            {
                                terminate = true;
                                cerr << "error: Could not open " << argv[argc - 1] << "." << endl;
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

                    put_classifiers(pClassifier1, pClassifier2);

                    cout << "\n";
                    cout << "1st classifier: "
                         << global.time1.tv_sec << "."
                         << global.time1.tv_nsec
                         << endl;
                    cout << "2nd classifier: "
                         << global.time2.tv_sec << "."
                         << global.time2.tv_nsec
                         << endl;
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
