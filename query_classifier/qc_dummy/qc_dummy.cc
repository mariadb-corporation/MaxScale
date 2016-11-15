/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/modules.h>
#include <maxscale/query_classifier.h>

qc_parse_result_t qc_parse(GWBUF* querybuf)
{
    return QC_QUERY_INVALID;
}

uint32_t qc_get_type(GWBUF* querybuf)
{
    return QUERY_TYPE_UNKNOWN;
}

char** qc_get_table_names(GWBUF* querybuf, int* tblsize, bool fullnames)
{
    *tblsize = 0;
    return NULL;
}

char* qc_get_created_table_name(GWBUF* querybuf)
{
    return NULL;
}

bool qc_is_real_query(GWBUF* querybuf)
{
    return false;
}

bool qc_is_drop_table_query(GWBUF* querybuf)
{
    return false;
}

bool qc_query_has_clause(GWBUF* buf)
{
    return false;
}

char** qc_get_database_names(GWBUF* querybuf, int* size)
{
    *size = 0;
    return NULL;
}

qc_query_op_t qc_get_operation(GWBUF* querybuf)
{
    return QUERY_OP_UNDEFINED;
}

char* qc_sqlite_get_prepare_name(GWBUF* query)
{
    return NULL;
}

qc_query_op_t qc_sqlite_get_prepare_operation(GWBUF* query)
{
    return QUERY_OP_UNDEFINED;
}

void qc_sqlite_get_field_info(GWBUF* query, const QC_FIELD_INFO** infos, size_t* n_infos)
{
    *infos = NULL;
    *n_infos = 0;
}

bool qc_init(const char* args)
{
    return true;
}

void qc_end(void)
{
}

bool qc_thread_init(void)
{
    return true;
}

void qc_thread_end(void)
{
}

extern "C"
{

    void ModuleInit()
    {
    }

    /* @see function load_module in load_utils.c for explanation of the following
     * lint directives.
    */
    /*lint -e14 */
    MODULE_INFO info =
    {
        MODULE_API_QUERY_CLASSIFIER,
        MODULE_IN_DEVELOPMENT,
        QUERY_CLASSIFIER_VERSION,
        const_cast<char*>("Dummy Query Classifier"),
    };

    static char version_string[] = "V1.0.0";

    char* version()
    {
        return const_cast<char*>(version_string);
    }

    static QUERY_CLASSIFIER qc =
    {
        qc_init,
        qc_end,
        qc_thread_init,
        qc_thread_end,
        qc_parse,
        qc_get_type,
        qc_get_operation,
        qc_get_created_table_name,
        qc_is_drop_table_query,
        qc_is_real_query,
        qc_get_table_names,
        NULL,
        qc_query_has_clause,
        qc_get_database_names,
        qc_get_prepare_name,
        qc_get_prepare_operation,
        qc_get_field_info,
    };

    QUERY_CLASSIFIER* GetModuleObject()
    {
        return &qc;
    }
    /*lint +e14 */
}
