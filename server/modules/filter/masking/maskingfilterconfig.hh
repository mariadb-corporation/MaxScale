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

#include <maxscale/ccdefs.hh>
#include <maxscale/config.h>
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

    static const char*          rules_name;

    static const char*          warn_type_mismatch_name;
    static const MXS_ENUM_VALUE warn_type_mismatch_values[];
    static const char*          warn_type_mismatch_default;

    static const char*          prevent_function_usage_name;
    static const char*          prevent_function_usage_default;

    MaskingFilterConfig(const char* zName, const MXS_CONFIG_PARAMETER* pParams)
        : m_name(zName)
        , m_large_payload(get_large_payload(pParams))
        , m_rules(get_rules(pParams))
        , m_warn_type_mismatch(get_warn_type_mismatch(pParams))
        , m_prevent_function_usage(get_prevent_function_usage(pParams))
    {}
    ~MaskingFilterConfig() {}

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

    static large_payload_t get_large_payload(const MXS_CONFIG_PARAMETER* pParams);
    static std::string get_rules(const MXS_CONFIG_PARAMETER* pParams);
    static warn_type_mismatch_t get_warn_type_mismatch(const MXS_CONFIG_PARAMETER* pParams);
    static bool get_prevent_function_usage(const MXS_CONFIG_PARAMETER* pParams);

private:
    std::string          m_name;
    large_payload_t      m_large_payload;
    std::string          m_rules;
    warn_type_mismatch_t m_warn_type_mismatch;
    bool                 m_prevent_function_usage;
};
