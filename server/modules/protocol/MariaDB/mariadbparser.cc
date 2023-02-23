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

#include <maxscale/protocol/mariadb/mariadbparser.hh>


MariaDBParser::MariaDBParser()
{
}

MariaDBParser::~MariaDBParser()
{
}


qc_parse_result_t MariaDBParser::parse(GWBUF* pStmt, uint32_t collect) const
{
    return qc_parse(pStmt, collect);
}

mxs::Parser::DatabaseNames MariaDBParser::get_database_names(GWBUF* pStmt) const
{
    return qc_get_database_names(pStmt);
}

void MariaDBParser::get_field_info(GWBUF* pStmt,
                                   const QC_FIELD_INFO** ppInfos,
                                   size_t* pnInfos) const
{
    qc_get_field_info(pStmt, ppInfos, pnInfos);
}

void MariaDBParser::get_function_info(GWBUF* pStmt,
                                      const QC_FUNCTION_INFO** ppInfos,
                                      size_t* pnInfos) const
{
    qc_get_function_info(pStmt, ppInfos, pnInfos);
}

qc_query_op_t MariaDBParser::get_operation(GWBUF* pStmt) const
{
    return qc_get_operation(pStmt);
}

uint32_t MariaDBParser::get_options() const
{
    return qc_get_options();
}

GWBUF* MariaDBParser::get_preparable_stmt(GWBUF* pStmt) const
{
    return qc_get_preparable_stmt(pStmt);
}

std::string_view MariaDBParser::get_prepare_name(GWBUF* pStmt) const
{
    return qc_get_prepare_name(pStmt);
}

MariaDBParser::TableNames MariaDBParser::get_table_names(GWBUF* pStmt) const
{
    return qc_get_table_names(pStmt);
}

uint32_t MariaDBParser::get_trx_type_mask(GWBUF* pStmt) const
{
    return qc_get_trx_type_mask(pStmt);
}

uint32_t MariaDBParser::get_type_mask(GWBUF* pStmt) const
{
    return qc_get_type_mask(pStmt);
}

bool MariaDBParser::set_options(uint32_t options)
{
    return qc_set_options(options);
}

