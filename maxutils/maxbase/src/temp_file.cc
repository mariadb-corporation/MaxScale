/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/temp_file.hh>
#include <maxbase/assert.hh>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace maxbase
{

TempFile::TempFile(const std::string& dir)
{
    m_name = dir + "/XXXXXX";
    auto fd = ::mkstemp(m_name.data());
    ::close(fd);
}

TempFile::TempFile(TempFile&& rhs)
    : m_name(std::move(rhs.m_name))
{
    rhs.m_name.clear();
}

std::string TempFile::name() const
{
    return m_name;
}

TempFile::~TempFile()
{
    if (!m_name.empty())
    {
        remove(m_name.c_str());
    }
}

TempDirectory::TempDirectory(const std::string& dir)
    : m_dir(dir)

{
    const std::string prefix = "/tmp/";
    if (m_dir.length() < prefix.length() + 1 || m_dir.substr(0, 5) != prefix)
    {
        m_valid = false;
        m_dir = "/not-a-valid-path";
        mxb_assert(m_valid);
        throw std::runtime_error("TempDirectory dir name must start with '/tmp/'");
    }
    else
    {
        std::filesystem::remove_all(m_dir);
        m_valid = std::filesystem::create_directories(m_dir);
        m_dir = std::filesystem::canonical(m_dir);
    }
}

bool TempDirectory::is_valid() const
{
    return m_valid;
}

TempDirectory::~TempDirectory()
{
    if (m_valid)
    {
        try
        {
            std::filesystem::remove_all(m_dir);
        }
        catch (...)
        {
            // oh no, but not to worry, if the constructor ran
            // this is more than unlikely to happen and doesn't run
            // on a crash anyway.
        }
    }
}

TempFile TempDirectory::temp_file() const
{
    mxb_assert(m_valid);
    return TempFile(m_dir);
}
}
