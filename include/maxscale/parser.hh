/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>


namespace maxscale
{

class Parser
{
public:
    using TableNames = std::vector<QcTableName>;
    using DatabaseNames = std::vector<std::string_view>;

    virtual ~Parser() = default;

    static bool type_mask_contains(uint32_t type_mask, qc_query_type_t type)
    {
        return qc_query_is_type(type_mask, type);
    }

    virtual qc_parse_result_t parse(GWBUF* pStmt, uint32_t collect) const = 0;

    virtual DatabaseNames    get_database_names(GWBUF* pStmt) const = 0;
    virtual void             get_field_info(GWBUF* pStmt,
                                            const QC_FIELD_INFO** ppInfos,
                                            size_t* pnInfos) const = 0;
    virtual void             get_function_info(GWBUF* pStmt,
                                               const QC_FUNCTION_INFO** infos,
                                               size_t* n_infos) const = 0;
    virtual qc_query_op_t    get_operation(GWBUF* pStmt) const = 0;
    virtual uint32_t         get_options() const = 0;
    virtual GWBUF*           get_preparable_stmt(GWBUF* pStmt) const = 0;
    virtual std::string_view get_prepare_name(GWBUF* pStmt) const = 0;
    virtual TableNames       get_table_names(GWBUF* pStmt) const = 0;
    virtual uint32_t         get_trx_type_mask(GWBUF* pStmt) const = 0;
    virtual uint32_t         get_type_mask(GWBUF* pStmt) const = 0;

    virtual bool set_options(uint32_t options) = 0;
};

}
