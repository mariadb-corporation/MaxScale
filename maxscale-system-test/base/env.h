/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <string>

namespace base
{

/* Environment variable. Usage:
 * Env user{"USER"};
 * std::string home = Env{"HOME"};
 * An environment variable can be empty() but
 * still is_defined().
 */
class Env : public std::string
{
public:
    Env(const std::string& name) : m_is_defined(false)
    {
        if (const char* var = getenv(name.c_str()))
        {
            m_is_defined = true;
            this->assign(var);
        }
    }

    bool is_defined() const
    {
        return m_is_defined;
    }
private:
    bool m_is_defined;
};
}   // base
