/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/string.hh>
#include <stdexcept>
#include <sstream>
#include <string>

namespace maxbase
{

class Exception : public std::runtime_error
{
public:
    /**
     * @brief Exception
     * @param msg   - => what()
     * @param code  - generic error code, covers a lot of cases (an enum of some kind)
     * @param file  - file name where exception was thrown
     * @param line  - line from where exception was thrown
     */
    Exception(const std::string& msg, int code, const std::string& file, int line, const std::string& type)
        : std::runtime_error(msg)
        , m_code(code)
        , m_file(file)
        , m_line(line)
        , m_type(type)
    {
    }

    std::string file() const
    {
        return m_file;
    }
    int line() const
    {
        return m_line;
    }
    int code() const
    {
        return m_code;
    }
    std::string type() const
    {
        return m_type;
    }
    std::string error_msg() const
    {
        return MAKE_STR(file() << ':' << line() << ' ' << m_type << ": " << what());
    }

private:
    int         m_code;
    std::string m_file;
    int         m_line;
    std::string m_type;
};
}

#define DEFINE_EXCEPTION(Type) \
    struct Type : public maxbase::Exception \
    { \
        using maxbase::Exception::Exception; \
    }

#define DEFINE_SUB_EXCEPTION(Super, Sub) \
    struct Sub : Super \
    { \
        using Super::Super; \
    }

#define MXB_THROW(Type, msg_str) \
    { \
        std::ostringstream os; \
        os << msg_str; \
        throw Type(os.str(), -1, __FILE__, __LINE__, #Type); \
    }

#define MXB_THROWCode(Type, code, msg_str) \
    { \
        std::ostringstream os; \
        os << msg_str; \
        throw Type(os.str(), code, __FILE__, __LINE__, #Type); \
    }
