/**
 * @section LICENCE
 *
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is
 * free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab
 *
 * @file
 *
 */

#include <query_classifier.h>
#include <log_manager.h>
#include <modules.h>

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

static const char default_qc_name[] = "qc_mysqlembedded";

static QUERY_CLASSIFIER* classifier;


bool qc_init(const char* plugin_name)
{
    QC_TRACE();
    ss_dassert(!classifier);

    if (!plugin_name || (*plugin_name == 0))
    {
        MXS_NOTICE("No query classifier specified, using default '%s'.", default_qc_name);
        plugin_name = default_qc_name;
    }

    bool success = false;
    classifier = qc_load(plugin_name);

    if (classifier)
    {
        success = classifier->qc_init();
    }

    return success;
}

void qc_end(void)
{
    QC_TRACE();
    ss_dassert(classifier);

    classifier->qc_end();
    classifier = NULL;
}

QUERY_CLASSIFIER* qc_load(const char* plugin_name)
{
    void* module = load_module(plugin_name, MODULE_QUERY_CLASSIFIER);

    if (module)
    {
        MXS_INFO("%s loaded.", plugin_name);
    }
    else
    {
        MXS_ERROR("Could not load %s.", plugin_name);
    }

    return (QUERY_CLASSIFIER*) module;
}

void qc_unload(QUERY_CLASSIFIER* classifier)
{
    // TODO: The module loading/unloading needs an overhaul before we
    // TODO: actually can unload something.
}

bool qc_thread_init(void)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_thread_init();
}

void qc_thread_end(void)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_thread_end();
}

/**
 * Returns a bitmask specifying the type(s) of the query.
 * The result should be tested against specific qc_query_type_t values
 * using the bitwise & operator, never using the == operator.
 *
 * @param query A buffer containing a query.
 *
 * @return A bitmask of type bits.
 */
uint32_t qc_get_type(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_type(query);
}

qc_query_op_t qc_get_operation(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_operation(query);
}

char* qc_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_created_table_name(query);
}

bool qc_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_is_drop_table_query(query);
}

bool qc_is_real_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_is_real_query(query);
}

char** qc_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_table_names(query, tblsize, fullnames);
}

char* qc_get_canonical(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_canonical(query);
}

bool qc_query_has_clause(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_query_has_clause(query);
}

/**
 * Generate a string of query type value.
 * Caller must free the memory of the resulting string.
 *
 * @param   qtype   Query type value, combination of values listed in
 *                  query_classifier.h
 *
 * @return  string representing the query type value
 */
char* qc_get_qtype_str(qc_query_type_t qtype)
{
    QC_TRACE();
    int t1 = (int) qtype;
    int t2 = 1;
    qc_query_type_t t = QUERY_TYPE_UNKNOWN;
    char* qtype_str = NULL;

    /**
     * Test values (bits) and clear matching bits from t1 one by one until
     * t1 is completely cleared.
     */
    while (t1 != 0)
    {
        if (t1 & t2)
        {
            t = (qc_query_type_t) t2;

            if (qtype_str == NULL)
            {
                qtype_str = strdup(STRQTYPE(t));
            }
            else
            {
                size_t len = strlen(STRQTYPE(t));
                /** reallocate space for delimiter, new string and termination */
                qtype_str = (char *) realloc(qtype_str, strlen(qtype_str) + 1 + len + 1);
                snprintf(qtype_str + strlen(qtype_str), 1 + len + 1, "|%s", STRQTYPE(t));
            }

            /** Remove found value from t1 */
            t1 &= ~t2;
        }

        t2 <<= 1;
    }

    return qtype_str;
}

char* qc_get_affected_fields(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_affected_fields(query);
}

char** qc_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_database_names(query, sizep);
}
