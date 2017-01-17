#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
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

    static const char*          rules_file_name;

    static const char*          warn_type_mismatch_name;
    static const MXS_ENUM_VALUE warn_type_mismatch_values[];
    static const char*          warn_type_mismatch_default;

    MaskingFilterConfig(const char* zName)
        : m_name(zName)
        , m_warn_type_mismatch(WARN_NEVER)
    {}
    ~MaskingFilterConfig() {}

    const std::string&   name() const
    {
        return m_name;
    }
    const std::string&   rules_file() const
    {
        return m_rules_file;
    }
    warn_type_mismatch_t warn_type_mismatch() const
    {
        return m_warn_type_mismatch;
    }

    void set_rules_file(const std::string& s)
    {
        m_rules_file = s;
    }
    void set_warn_type_mismatch(warn_type_mismatch_t w)
    {
        m_warn_type_mismatch = w;
    }

    static warn_type_mismatch_t get_warn_type_mismatch(const CONFIG_PARAMETER* pParams);

private:
    std::string          m_name;
    std::string          m_rules_file;
    warn_type_mismatch_t m_warn_type_mismatch;
};
