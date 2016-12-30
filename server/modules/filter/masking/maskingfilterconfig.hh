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
#include <string>

class MaskingFilterConfig
{
public:
    MaskingFilterConfig(const char* zName)
        : m_name(zName)
    {}
    ~MaskingFilterConfig() {}

    const std::string& name() const { return m_name; }
    const std::string& rules_file() const { return m_rules_file; }

    void set_rules_file(const std::string& s) { m_rules_file = s; }

private:
    std::string m_name;
    std::string m_rules_file;
};
