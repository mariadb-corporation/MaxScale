/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <map>
#include <maxscale/ccdefs.hh>
#include <maxscale/qc_stmt_info.hh>
#include <maxscale/parser.hh>

/**
 * QUERY_CLASSIFIER defines the object a query classifier plugin must
 * implement and return.
 *
 * To a user of the query classifier functionality, it can in general
 * be ignored.
 */
class QUERY_CLASSIFIER
{
public:
    /**
     * Called once to setup the query classifier
     *
     * @param sql_mode  The default sql mode.
     * @param args      The value of `query_classifier_args` in the configuration file.
     *
     * @return QC_RESULT_OK, if the query classifier could be setup, otherwise
     *         some specific error code.
     */
    virtual int32_t setup(qc_sql_mode_t sql_mode, const char* args) = 0;

    /**
     * Called once per each thread.
     *
     * @return QC_RESULT_OK, if the thread initialization succeeded.
     */
    virtual int32_t thread_init(void) = 0;

    /**
     * Called once when a thread finishes.
     */
    virtual void thread_end(void) = 0;

    /**
     * Return statement currently being classified.
     *
     * @param ppStmp  Pointer to pointer that on return will point to the
     *                statement being classified.
     * @param pLen    Pointer to value that on return will contain the length
     *                of the returned string.
     *
     * @return QC_RESULT_OK if a statement was returned (i.e. a statement is being
     *         classified), QC_RESULT_ERROR otherwise.
     */
    virtual int32_t get_current_stmt(const char** ppStmt, size_t* pLen) = 0;

    /**
     * Get result from info.
     *
     * @param  The info whose result should be returned.
     *
     * @return The result of the provided info.
     */
    virtual QC_STMT_RESULT get_result_from_info(const QC_STMT_INFO* info) = 0;

    /**
     * Get canonical statement
     *
     * @param info  The info whose canonical statement should be returned.
     *
     * @attention - The string_view refers to data that remains valid only as long
     *              as @c info remains valid.
     *            - If @c info is of a COM_STMT_PREPARE, then the canonical string will
     *              be suffixed by ":P".
     *
     * @return The canonical statement.
     */
    virtual std::string_view info_get_canonical(const QC_STMT_INFO* info) = 0;

    virtual mxs::Parser& parser() = 0;
};
