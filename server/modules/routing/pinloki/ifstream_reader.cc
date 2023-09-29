/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "ifstream_reader.hh"
#include "config.hh"
#include <maxbase/exception.hh>
#include <maxbase/assert.hh>
#include <thread>
#include <cstring>

namespace pinloki
{

namespace
{

const mxb::Duration SLEEP_DURATION = 250us;

#define CHECK_IFS() \
    do \
    { \
        if (m_ifs.fail()) \
        { \
            MXB_THROW(BinlogReadError, \
                      "IFStreamReader error: " << errno << ", " << mxb_strerror(errno)); \
        } \
    } while (false)
}

IFStreamReader::IFStreamReader(const std::string& file_name)
    : m_ifs(file_name)
{
}

IFStreamReader::IFStreamReader(std::ifstream&& ifs)
    : m_ifs(std::move(ifs))
{
    mxb_assert(m_ifs.tellg() == 0);
}

inline void IFStreamReader::update_in_avail(ssize_t requested)
{
    // This is "expensive", but for complete files the branch
    // is taken only once. For files being written to, and the
    // reader is ahead (rare), the cost is acceptable.
    if (m_in_avail < requested)
    {
        m_ifs.seekg(0, std::ios_base::end);
        m_in_avail = m_ifs.tellg() - m_bytes_read;
        m_ifs.seekg(m_bytes_read);
    }
}

bool IFStreamReader::is_open() const
{
    return m_ifs.is_open();
}

void IFStreamReader::close()
{
    m_ifs.close();
}

ssize_t IFStreamReader::advance(ssize_t nbytes)
{
    mxb_assert(m_putback_buffer.empty());

    m_ifs.ignore(nbytes);
    CHECK_IFS();

    auto bytes_advanced = m_ifs.gcount();
    m_bytes_read += bytes_advanced;
    m_in_avail -= bytes_advanced;

    return bytes_advanced;
}

ssize_t IFStreamReader::advance_for(ssize_t nbytes, mxb::Duration timeout)
{
    mxb_assert(m_putback_buffer.empty());

    ssize_t bytes_advanced = 0;
    mxb::StopWatch sw;

    while (bytes_advanced < nbytes && sw.split() < timeout)
    {
        update_in_avail(nbytes - bytes_advanced);
        auto skip = std::min(m_ifs.rdbuf()->in_avail(), nbytes - bytes_advanced);
        m_ifs.ignore(skip);
        CHECK_IFS();
        bytes_advanced += skip;
        m_in_avail -= skip;
        if (bytes_advanced < nbytes)
        {
            std::this_thread::sleep_for(SLEEP_DURATION);
        }
    }

    m_bytes_read += bytes_advanced;

    return bytes_advanced;
}

bool IFStreamReader::read_n(char* pData, ssize_t nbytes)
{
    if (!m_putback_buffer.empty())
    {
        mxb_assert(m_putback_buffer.size() == size_t(nbytes));

        std::memcpy(pData, m_putback_buffer.data(), nbytes);
        m_bytes_read += nbytes;
        m_putback_buffer.clear();
        return true;
    }

    bool ret = false;
    update_in_avail(nbytes);

    if (m_in_avail >= nbytes)
    {
        m_ifs.read(pData, nbytes);
        CHECK_IFS();
        mxb_assert(nbytes == m_ifs.gcount());
        m_bytes_read += nbytes;
        m_in_avail -= nbytes;
        ret = true;
    }

    return ret;
}

bool IFStreamReader::read_n_for(char* pData, ssize_t nbytes, mxb::Duration timeout)
{
    mxb::StopWatch sw;

    if (!m_putback_buffer.empty())
    {
        mxb_assert(m_putback_buffer.size() == size_t(nbytes));

        std::memcpy(pData, m_putback_buffer.data(), nbytes);
        m_bytes_read += nbytes;
        m_putback_buffer.clear();
        return true;
    }

    update_in_avail(nbytes);
    while (m_in_avail < nbytes
           && sw.split() < timeout)
    {
        CHECK_IFS();
        std::this_thread::sleep_for(SLEEP_DURATION);
        update_in_avail(nbytes);
    }

    return read_n(pData, nbytes);
}

void IFStreamReader::put_back(std::vector<char>&& data)
{
    mxb_assert(m_putback_buffer.empty());

    m_bytes_read -= data.size();
    m_putback_buffer = std::move(data);
}

bool IFStreamReader::at_pos(ssize_t pos) const
{
    mxb_assert(m_bytes_read == const_cast<std::ifstream&>(m_ifs).tellg());
    mxb_assert(m_bytes_read == pos);

    return m_bytes_read == pos;
}
}
