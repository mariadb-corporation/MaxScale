#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>
#include <maxscale/query_classifier.h>

MXS_BEGIN_DECLS

typedef enum qc_trx_parse_using
{
    QC_TRX_PARSE_USING_QC,     /**< Use the query classifier. */
    QC_TRX_PARSE_USING_PARSER, /**< Use custom parser. */
} qc_trx_parse_using_t;

/**
 * Returns the type bitmask of transaction related statements.
 *
 * @param stmt  A COM_QUERY or COM_STMT_PREPARE packet.
 * @param use   What method should be used.
 *
 * @return The relevant type bits if the statement is transaction
 *         related, otherwise 0.
 *
 * @see qc_get_trx_type_mask
 */
uint32_t qc_get_trx_type_mask_using(GWBUF* stmt, qc_trx_parse_using_t use);

MXS_END_DECLS
