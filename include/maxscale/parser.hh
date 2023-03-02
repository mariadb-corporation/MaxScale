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
#include <maxscale/query_classifier.hh>


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
        return (type_mask & (uint32_t)type) == (uint32_t)type;
    }

    static std::string type_mask_to_string(uint32_t type_mask);

    static const char* op_to_string(qc_query_op_t op);

    // Only for testing purposes, not to be used for anything else.
    virtual QUERY_CLASSIFIER& classifier() const = 0;

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
    virtual bool             is_drop_table_query(GWBUF* pStmt) const = 0;

    virtual bool set_options(uint32_t options) = 0;
    virtual void set_server_version(uint64_t version) = 0;
    virtual void set_sql_mode(qc_sql_mode_t sql_mode) = 0;
};

class CachingParser : public mxs::Parser
{
public:
    CachingParser(const CachingParser&) = delete;
    CachingParser& operator = (const CachingParser&) = delete;

    QUERY_CLASSIFIER& classifier() const override;

    qc_parse_result_t parse(GWBUF* pStmt, uint32_t collect) const override;

    DatabaseNames    get_database_names(GWBUF* pStmt) const override;
    void             get_field_info(GWBUF* pStmt,
                                    const QC_FIELD_INFO** ppInfos,
                                    size_t* pnInfos) const override;
    void             get_function_info(GWBUF* pStmt,
                                       const QC_FUNCTION_INFO** infos,
                                       size_t* n_infos) const override;
    qc_query_op_t    get_operation(GWBUF* pStmt) const override;
    uint32_t         get_options() const override;
    GWBUF*           get_preparable_stmt(GWBUF* pStmt) const override;
    std::string_view get_prepare_name(GWBUF* pStmt) const override;
    TableNames       get_table_names(GWBUF* pStmt) const override;
    uint32_t         get_trx_type_mask(GWBUF* pStmt) const override;
    uint32_t         get_type_mask(GWBUF* pStmt) const override;
    bool             is_drop_table_query(GWBUF* pStmt) const override;

    bool set_options(uint32_t options) override;
    void set_sql_mode(qc_sql_mode_t sql_mode) override;
    void set_server_version(uint64_t version) override;

protected:
    CachingParser(QUERY_CLASSIFIER* pClassifier)
        : m_classifier(*pClassifier)
    {
    }

    QUERY_CLASSIFIER& m_classifier;
};

}
