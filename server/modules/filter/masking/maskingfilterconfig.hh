/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/modinfo.hh>
#include <string>

class MaskingFilterConfig : public mxs::config::Configuration
{
public:
    MaskingFilterConfig(const MaskingFilterConfig&) = delete;
    MaskingFilterConfig& operator = (const MaskingFilterConfig&) = delete;

    enum warn_type_mismatch_t
    {
        WARN_NEVER,
        WARN_ALWAYS
    };

    enum large_payload_t
    {
        LARGE_IGNORE,
        LARGE_ABORT
    };

    MaskingFilterConfig(const char* zName);

    MaskingFilterConfig(MaskingFilterConfig&&) = default;

    ~MaskingFilterConfig()
    {
    }

    const std::string& name() const
    {
        return m_name;
    }

    large_payload_t large_payload() const
    {
        return m_large_payload;
    }

    const std::string& rules() const
    {
        return m_rules;
    }

    warn_type_mismatch_t warn_type_mismatch() const
    {
        return m_warn_type_mismatch;
    }

    bool prevent_function_usage() const
    {
        return m_prevent_function_usage;
    }

    bool check_user_variables() const
    {
        return m_check_user_variables;
    }

    bool check_unions() const
    {
        return m_check_unions;
    }

    bool check_subqueries() const
    {
        return m_check_subqueries;
    }

    bool require_fully_parsed() const
    {
        return m_require_fully_parsed;
    }

    bool treat_string_arg_as_field() const
    {
        return m_treat_string_arg_as_field;
    }

    bool is_parsing_needed() const
    {
        return prevent_function_usage() || check_user_variables() || check_unions() || check_subqueries();
    }

    static void populate(MXS_MODULE& info);

private:
    std::string          m_name;
    large_payload_t      m_large_payload;
    std::string          m_rules;
    warn_type_mismatch_t m_warn_type_mismatch;
    bool                 m_prevent_function_usage;
    bool                 m_check_user_variables;
    bool                 m_check_unions;
    bool                 m_check_subqueries;
    bool                 m_require_fully_parsed;
    bool                 m_treat_string_arg_as_field;
};
