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

#include <log_manager.h>
#include <modules.h>
#include <query_classifier.h>

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

static QUERY_CLASSIFIER* classifier;
static const char qc_mysqlembedded[] = "qc_mysqlembedded";


static bool qc_pp_init(void)
{
    QC_TRACE();
    ss_dassert(!classifier);

    bool success = false;
    void* module = load_module(qc_mysqlembedded, MODULE_QUERY_CLASSIFIER);

    if (module)
    {
        classifier = (QUERY_CLASSIFIER*) module;
        MXS_NOTICE("%s loaded.", qc_mysqlembedded);

        success = classifier->qc_init();
    }
    else
    {
        MXS_ERROR("Could not load %s.", qc_mysqlembedded);
    }

    return success;
}

static void qc_pp_end(void)
{
    QC_TRACE();
    ss_dassert(classifier);

    classifier->qc_end();
    classifier = NULL;
}

static bool qc_pp_thread_init(void)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_thread_init();
}

static void qc_pp_thread_end(void)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_thread_end();
}

static qc_query_type_t qc_pp_get_type(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_type(query);
}

static qc_query_op_t qc_pp_get_operation(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_operation(query);
}

static char* qc_pp_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_created_table_name(query);
}

static bool qc_pp_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_is_drop_table_query(query);
}

static bool qc_pp_is_real_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_is_real_query(query);
}

static char** qc_pp_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_table_names(query, tblsize, fullnames);
}

static char* qc_pp_get_canonical(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_canonical(query);
}

static bool qc_pp_query_has_clause(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_query_has_clause(query);
}

static char* qc_pp_get_qtype_str(qc_query_type_t qtype)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_qtype_str(qtype);
}

static char* qc_pp_get_affected_fields(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_affected_fields(query);
}

static char** qc_pp_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    ss_dassert(classifier);

    return classifier->qc_get_database_names(query, sizep);
}

/**
 * EXPORTS
 */

static char version_string[] = "V1.0.0";

static QUERY_CLASSIFIER qc =
{
    qc_pp_init,
    qc_pp_end,
    qc_pp_thread_init,
    qc_pp_thread_end,
    qc_pp_get_type,
    qc_pp_get_operation,
    qc_pp_get_created_table_name,
    qc_pp_is_drop_table_query,
    qc_pp_is_real_query,
    qc_pp_get_table_names,
    qc_pp_get_canonical,
    qc_pp_query_has_clause,
    qc_pp_get_qtype_str,
    qc_pp_get_affected_fields,
    qc_pp_get_database_names,
};


MODULE_INFO info =
{
    MODULE_API_QUERY_CLASSIFIER,
    MODULE_IN_DEVELOPMENT,
    QUERY_CLASSIFIER_VERSION,
    "Query classifier using external process.",
};

char* version()
{
    return version_string;
}

void ModuleInit()
{
}

QUERY_CLASSIFIER* GetModuleObject()
{
    return &qc;
}
