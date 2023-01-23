/*
 * Copyright (c) 2023 MariaDB Corporation Ab
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

#include <maxbase/csv_writer.hh>
#include <maxbase/assert.hh>
#include <sys/stat.h>

namespace maxbase
{

CsvWriter::CsvWriter(const std::string& path,
                     const std::string& file_base_name,
                     const std::vector<std::string>& columns)
    : m_path(path)
    , m_base_name(file_base_name)
    , m_columns(columns)
{
    mxb_assert(!path.empty());
    mxb_assert(!m_base_name.empty());
    mxb_assert(!m_columns.empty());

    if (m_path.back() != '/')
    {
        m_path += '/';
    }

    m_full_path = m_path + m_base_name + ".csv";

    open_file();
}

bool CsvWriter::add_row(std::vector<std::string>& values)
{
    mxb_assert(m_columns.size() == values.size());

    return write(values);
}

bool CsvWriter::rotate()
{
    return open_file();
}

const std::string CsvWriter::full_path() const
{
    return m_full_path;
}

bool CsvWriter::open_file()
{
    bool ok = true;

    m_file = std::ofstream(m_full_path, std::ios_base::app);

    ok = m_file.good();

    if (ok && m_file.tellp() == 0)
    {
        ok = write(m_columns);
    }

    return ok;
}

bool CsvWriter::write(std::vector<std::string>& values)
{
    bool first = true;

    for (auto& v : values)
    {
        if (!first)
        {
            m_file << ',';
        }

        m_file << '"' << inplace_escape(v) << '"';

        first = false;
    }

    m_file << std::endl;

    return m_file.good();
}

std::string& CsvWriter::inplace_escape(std::string& str)
{
    std::string modified_str;       // optimization: don't touch str, if there are no quotes
    bool modified = false;
    for (size_t i = 0, len = str.size(); i < len; ++i)
    {
        auto ch = str[i];
        if (ch == '"')
        {
            if (!modified)
            {
                modified_str = str.substr(0, i);
                modified = true;
            }

            modified_str += '"';
        }

        if (modified)
        {
            modified_str += ch;
        }
    }

    if (modified)
    {
        str.swap(modified_str);
    }

    return str;
}
}
