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
    void* module = load_module(plugin_name, MODULE_QUERY_CLASSIFIER);

    if (module)
    {
        classifier = (QUERY_CLASSIFIER*) module;
        MXS_NOTICE("%s loaded.", plugin_name);

        success = classifier->qc_init();
    }
    else
    {
        MXS_ERROR("Could not load %s.", plugin_name);
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

qc_query_type_t qc_get_type(GWBUF* query)
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

char* qc_get_qtype_str(qc_query_type_t qtype)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_qtype_str(qtype);
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
