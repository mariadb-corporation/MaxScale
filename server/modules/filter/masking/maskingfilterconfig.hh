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
#include <maxscale/config.hh>
#include <maxscale/modinfo.h>
#include <string>

class MaskingFilterConfig
{
public:
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

    static const char*          large_payload_name;
    static const MXS_ENUM_VALUE large_payload_values[];
    static const char*          large_payload_default;

    static const char* rules_name;

    static const char*          warn_type_mismatch_name;
    static const MXS_ENUM_VALUE warn_type_mismatch_values[];
    static const char*          warn_type_mismatch_default;

    static const char* prevent_function_usage_name;
    static const char* prevent_function_usage_default;

    static const char* check_user_variables_name;
    static const char* check_user_variables_default;

    static const char* check_unions_name;
    static const char* check_unions_default;

    static const char* check_subqueries_name;
    static const char* check_subqueries_default;

    static const char* require_fully_parsed_name;
    static const char* require_fully_parsed_default;

    static const char* treat_string_arg_as_field_name;
    static const char* treat_string_arg_as_field_default;

    MaskingFilterConfig(const char* zName, const MXS_CONFIG_PARAMETER* pParams)
        : m_name(zName)
        , m_large_payload(get_large_payload(pParams))
        , m_rules(get_rules(pParams))
        , m_warn_type_mismatch(get_warn_type_mismatch(pParams))
        , m_prevent_function_usage(get_prevent_function_usage(pParams))
        , m_check_user_variables(get_check_user_variables(pParams))
        , m_check_unions(get_check_unions(pParams))
        , m_check_subqueries(get_check_subqueries(pParams))
        , m_require_fully_parsed(get_require_fully_parsed(pParams))
        , m_treat_string_arg_as_field(get_treat_string_arg_as_field(pParams))
    {
    }

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

    void set_large_payload(large_payload_t l)
    {
        m_large_payload = l;
    }

    void set_rules(const std::string& s)
    {
        m_rules = s;
    }
    void set_warn_type_mismatch(warn_type_mismatch_t w)
    {
        m_warn_type_mismatch = w;
    }

    void set_prevent_function_usage(bool b)
    {
        m_prevent_function_usage = b;
    }

    void set_check_user_variables(bool b)
    {
        m_check_user_variables = b;
    }

    void set_check_unions(bool b)
    {
        m_check_unions = b;
    }

    void set_check_subqueries(bool b)
    {
        m_check_subqueries = b;
    }

    void set_require_fully_parsed(bool b)
    {
        m_require_fully_parsed = b;
    }

    void set_treat_string_arg_as_field(bool b)
    {
        m_treat_string_arg_as_field = b;
    }

    bool is_parsing_needed() const
    {
        return prevent_function_usage() || check_user_variables() || check_unions() || check_subqueries();
    }

    static large_payload_t      get_large_payload(const MXS_CONFIG_PARAMETER* pParams);
    static std::string          get_rules(const MXS_CONFIG_PARAMETER* pParams);
    static warn_type_mismatch_t get_warn_type_mismatch(const MXS_CONFIG_PARAMETER* pParams);
    static bool                 get_prevent_function_usage(const MXS_CONFIG_PARAMETER* pParams);
    static bool                 get_check_user_variables(const MXS_CONFIG_PARAMETER* pParams);
    static bool                 get_check_unions(const MXS_CONFIG_PARAMETER* pParams);
    static bool                 get_check_subqueries(const MXS_CONFIG_PARAMETER* pParams);
    static bool                 get_require_fully_parsed(const MXS_CONFIG_PARAMETER* pParams);
    static bool                 get_treat_string_arg_as_field(const MXS_CONFIG_PARAMETER* pParams);

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
