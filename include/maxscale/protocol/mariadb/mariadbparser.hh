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
#include <maxscale/parser.hh>


class MariaDBParser : public maxscale::Parser
{
public:
    MariaDBParser();

    ~MariaDBParser();

    qc_parse_result_t parse(GWBUF* pStmt, uint32_t collect) const override;

    DatabaseNames    get_database_names(GWBUF* pStmt) const override;
    void             get_field_info(GWBUF* pStmt,
                                    const QC_FIELD_INFO** ppInfos,
                                    size_t* pnInfos) const override;
    void             get_function_info(GWBUF* pStmt,
                                       const QC_FUNCTION_INFO** ppInfos,
                                       size_t* pninfos) const override;
    uint32_t         get_options() const override;
    qc_query_op_t    get_operation(GWBUF* pStmt) const override;
    GWBUF*           get_preparable_stmt(GWBUF* pStmt) const override;
    std::string_view get_prepare_name(GWBUF* pStmt) const override;
    TableNames       get_table_names(GWBUF* pStmt) const override;
    uint32_t         get_trx_type_mask(GWBUF* pStmt) const override;
    uint32_t         get_type_mask(GWBUF* pStmt) const override;
    bool             is_drop_table_query(GWBUF* pStmt) const override;

    bool set_options(uint32_t options) override;
};
