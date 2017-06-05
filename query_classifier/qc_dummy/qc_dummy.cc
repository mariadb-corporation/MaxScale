/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "qc_sqlite"
#include <maxscale/query_classifier.h>

#include "../../server/core/maxscale/config.h"

int32_t qc_dummy_parse(GWBUF* querybuf, uint32_t collect, int32_t* pResult)
{
    *pResult = QC_QUERY_INVALID;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_type_mask(GWBUF* querybuf, uint32_t* pType_mask)
{
    *pType_mask = QUERY_TYPE_UNKNOWN;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_table_names(GWBUF* querybuf, int32_t fullnames, char*** ppzNames, int32_t* pSize)
{
    *ppzNames = NULL;
    *pSize = 0;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_created_table_name(GWBUF* querybuf, char** pzName)
{
    *pzName = NULL;
    return QC_RESULT_OK;
}

int32_t qc_dummy_is_drop_table_query(GWBUF* querybuf, int32_t* pIs_drop_table)
{
    *pIs_drop_table = 0;
    return QC_RESULT_OK;
}

int32_t qc_dummy_query_has_clause(GWBUF* buf, int32_t *pHas_clause)
{
    *pHas_clause = 0;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_database_names(GWBUF* querybuf, char*** ppzNames, int32_t* pSize)
{
    *ppzNames = NULL;
    *pSize = 0;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_operation(GWBUF* querybuf, int32_t* pOp)
{
    *pOp = QUERY_OP_UNDEFINED;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_prepare_name(GWBUF* query, char** pzName)
{
    *pzName = NULL;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_field_info(GWBUF* query, const QC_FIELD_INFO** ppInfos, uint32_t* nInfos)
{
    *ppInfos = NULL;
    *nInfos = 0;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_function_info(GWBUF* query, const QC_FUNCTION_INFO** ppInfos, uint32_t* nInfos)
{
    *ppInfos = NULL;
    *nInfos = 0;
    return QC_RESULT_OK;
}

int32_t qc_dummy_setup(qc_sql_mode_t sql_mode, const char* args)
{
    return QC_RESULT_OK;
}

int32_t qc_dummy_process_init(void)
{
    return QC_RESULT_OK;
}

void qc_dummy_process_end(void)
{
}

int32_t qc_dummy_thread_init(void)
{
    return QC_RESULT_OK;
}

void qc_dummy_thread_end(void)
{
}

int32_t qc_dummy_get_preparable_stmt(GWBUF* stmt, GWBUF** preparable_stmt)
{
    *preparable_stmt = NULL;
    return QC_RESULT_OK;
}

int32_t qc_dummy_get_sql_mode(qc_sql_mode_t* sql_mode)
{
    return QC_RESULT_ERROR;
}

int32_t qc_dummy_set_sql_mode(qc_sql_mode_t sql_mode)
{
    return QC_RESULT_ERROR;
}

extern "C"
{
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static QUERY_CLASSIFIER qc =
        {
            qc_dummy_setup,
            qc_dummy_process_init,
            qc_dummy_process_end,
            qc_dummy_thread_init,
            qc_dummy_thread_end,
            qc_dummy_parse,
            qc_dummy_get_type_mask,
            qc_dummy_get_operation,
            qc_dummy_get_created_table_name,
            qc_dummy_is_drop_table_query,
            qc_dummy_get_table_names,
            NULL,
            qc_dummy_query_has_clause,
            qc_dummy_get_database_names,
            qc_dummy_get_prepare_name,
            qc_dummy_get_field_info,
            qc_dummy_get_function_info,
            qc_dummy_get_preparable_stmt,
            qc_dummy_get_sql_mode,
            qc_dummy_set_sql_mode,
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_QUERY_CLASSIFIER,
            MXS_MODULE_IN_DEVELOPMENT,
            QUERY_CLASSIFIER_VERSION,
            "Dummy Query Classifier",
            "V1.0.0",
            &qc,
            qc_dummy_process_init,
            qc_dummy_process_end,
            qc_dummy_thread_init,
            qc_dummy_thread_end,
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}
