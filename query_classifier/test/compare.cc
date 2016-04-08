/*
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2016
 *
 */

#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <gwdirs.h>
#include <query_classifier.h>
using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::ifstream;
using std::istream;
using std::ostream;
using std::string;

namespace
{

char USAGE[] = "usage: compare [file]";

const char* to_string(qc_query_op_t op)
{
    switch (op)
    {
    case QUERY_OP_UNDEFINED:
        return "QUERY_OP_UNDEFINED";
    case QUERY_OP_SELECT:
        return "QUERY_OP_SELECT";
    case QUERY_OP_UPDATE:
        return "QUERY_OP_UPDATE";
    case QUERY_OP_INSERT:
        return "QUERY_OP_INSERT";
    case QUERY_OP_DELETE:
        return "QUERY_OP_DELETE";
    case QUERY_OP_TRUNCATE:
        return "QUERY_OP_TRUNCATE";
    case QUERY_OP_ALTER:
        return "QUERY_OP_ALTER";
    case QUERY_OP_CREATE:
        return "QUERY_OP_CREATE";
    case QUERY_OP_DROP:
        return "QUERY_OP_DROP";
    case QUERY_OP_CHANGE_DB:
        return "QUERY_OP_CHANGE_DB";
    case QUERY_OP_LOAD:
        return "QUERY_OP_LOAD";
    case QUERY_OP_GRANT:
        return "QUERY_OP_GRANT";
    case QUERY_OP_REVOKE:
        return "QUERY_OP_REVOKE";
    default:
        return "UNKNOWN";
    }
}

GWBUF* create_gwbuf(const string& s)
{
    size_t len = s.length();
    size_t gwbuf_len = len + 6;

    GWBUF* gwbuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(gwbuf))) = len;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 1)) = (len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 2)) = (len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(gwbuf) + 5, s.data(), s.length());

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

QUERY_CLASSIFIER* get_classifier(const char* name)
{
    QUERY_CLASSIFIER* pClassifier = load_classifier(name);

    if (pClassifier)
    {
        if (!pClassifier->qc_init())
        {
            cerr << "error: Could not init classifier " << name << "." << endl;
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
        pClassifier->qc_end();
        qc_unload(pClassifier);
    }
}

bool get_classifiers(const char* name1, QUERY_CLASSIFIER** ppClassifier1,
                     const char* name2, QUERY_CLASSIFIER** ppClassifier2)
{
    bool rc = false;

    QUERY_CLASSIFIER* pClassifier1 = get_classifier(name1);

    if (pClassifier1)
    {
        QUERY_CLASSIFIER* pClassifier2 = get_classifier(name2);

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

void compare_get_type(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                      QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_get_type              : ";

    uint32_t rv1 = pClassifier1->qc_get_type(pCopy1);
    uint32_t rv2 = pClassifier2->qc_get_type(pCopy2);

    if (rv1 == rv2)
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: " << rv1 << " versus " << rv2 << endl;
    }
}

void compare_get_operation(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                           QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_get_operation         : ";

    qc_query_op_t rv1 = pClassifier1->qc_get_operation(pCopy1);
    qc_query_op_t rv2 = pClassifier2->qc_get_operation(pCopy2);

    if (rv1 == rv2)
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: " << to_string(rv1) << " versus " << to_string(rv2) << endl;
    }
}

void compare_get_created_table_name(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                    QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_get_created_table_name: ";

    char* rv1 = pClassifier1->qc_get_created_table_name(pCopy1);
    char* rv2 = pClassifier2->qc_get_created_table_name(pCopy2);

    if ((!rv1 && !rv2) || (rv1 && rv2 && (strcmp(rv1, rv2) == 0)))
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: " << (rv1 ? rv1 : "NULL") << " versus " << (rv2 ? rv2 : "NULL") << endl;
    }

    free(rv1);
    free(rv2);
}

void compare_is_drop_table_query(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                 QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_is_drop_table_query   : ";

    bool rv1 = pClassifier1->qc_is_drop_table_query(pCopy1);
    bool rv2 = pClassifier2->qc_is_drop_table_query(pCopy2);

    if (rv1 == rv2)
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: " << rv1 << " versus " << rv2 << endl;
    }
}

void compare_is_real_query(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                           QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_is_real_query         : ";

    bool rv1 = pClassifier1->qc_is_real_query(pCopy1);
    bool rv2 = pClassifier2->qc_is_real_query(pCopy2);

    if (rv1 == rv2)
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: " << rv1 << " versus " << rv2 << endl;
    }
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

void compare_get_table_names(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                             QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_get_table_names       : ";

    int n1 = 0;
    int n2 = 0;

    char** rv1 = pClassifier1->qc_get_table_names(pCopy1, &n1, false);
    char** rv2 = pClassifier2->qc_get_table_names(pCopy2, &n2, false);

    if ((!rv1 && !rv2) || ((n1 == n2) && compare_strings(rv1, rv2, n1)))
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: ";
        print_names(cout, rv1, n1);
        cout << " versus ";
        print_names(cout, rv2, n2);
        cout << endl;
    }

    free_strings(rv1, n1);
    free_strings(rv2, n2);
}

void compare_query_has_clause(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                              QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_query_has_clause      : ";

    bool rv1 = pClassifier1->qc_query_has_clause(pCopy1);
    bool rv2 = pClassifier2->qc_query_has_clause(pCopy2);

    if (rv1 == rv2)
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: " << rv1 << " versus " << rv2 << endl;
    }
}

void compare_get_affected_fields(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                 QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_get_affected_fields   : ";

    char* rv1 = pClassifier1->qc_get_affected_fields(pCopy1);
    char* rv2 = pClassifier2->qc_get_affected_fields(pCopy2);

    if ((!rv1 && !rv2) || (rv1 && rv2 && (strcmp(rv1, rv2) == 0)))
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: " << (rv1 ? rv1 : "NULL") << " versus " << (rv2 ? rv2 : "NULL") << endl;
    }

    free(rv1);
    free(rv2);
}

void compare_get_database_names(QUERY_CLASSIFIER* pClassifier1, GWBUF* pCopy1,
                                QUERY_CLASSIFIER* pClassifier2, GWBUF* pCopy2)
{
    cout << "qc_get_database_names    : ";

    int n1 = 0;
    int n2 = 0;

    char** rv1 = pClassifier1->qc_get_database_names(pCopy1, &n1);
    char** rv2 = pClassifier2->qc_get_database_names(pCopy2, &n2);

    if ((!rv1 && !rv2) || ((n1 == n2) && compare_strings(rv1, rv2, n1)))
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "MISMATCH: ";
        print_names(cout, rv1, n1);
        cout << " versus ";
        print_names(cout, rv2, n2);
        cout << endl;
    }

    free_strings(rv1, n1);
    free_strings(rv2, n2);
}


void compare(QUERY_CLASSIFIER* pClassifier1, QUERY_CLASSIFIER* pClassifier2, const string& s)
{
    GWBUF* pCopy1 = create_gwbuf(s);
    GWBUF* pCopy2 = create_gwbuf(s);

    compare_get_type(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_get_operation(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_get_created_table_name(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_is_drop_table_query(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_is_real_query(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_get_table_names(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_query_has_clause(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_get_affected_fields(pClassifier1, pCopy1, pClassifier2, pCopy2);
    compare_get_database_names(pClassifier1, pCopy1, pClassifier2, pCopy2);

    gwbuf_free(pCopy1);
    gwbuf_free(pCopy2);

    cout << endl;
}

int run(istream& in)
{
    set_datadir(strdup("/tmp"));
    set_langdir(strdup("."));
    set_process_datadir(strdup("/tmp"));

    QUERY_CLASSIFIER* pSqlite;
    QUERY_CLASSIFIER* pMySQL;

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
    {
        if (get_classifiers("qc_mysqlembedded", &pMySQL,
                            "qc_sqlite", &pSqlite))
        {
            string line;
            while (std::getline(in, line))
            {
                cout << line << endl;

                compare(pMySQL, pSqlite, line);
            }

            put_classifiers(pMySQL, pSqlite);
        }

        mxs_log_finish();
    }
    else
    {
        cerr << "error: Could not initialize log." << endl;
    }

    return EXIT_SUCCESS;
}

}

int main(int argc, char* argv[])
{
    int rc = EXIT_FAILURE;

    switch (argc)
    {
    case 1:
        rc = run(cin);
        break;

    case 2:
        {
            ifstream in(argv[1]);

            if (in)
            {
                rc = run(in);
            }
            else
            {
                cerr << "error: Could not open " << argv[1] << "." << endl;
            }
        }
        break;

    default:
        cout << USAGE << endl;
    }

    return rc;
}
