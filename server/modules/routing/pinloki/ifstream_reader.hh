/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/stopwatch.hh>
#include <fstream>
#include <vector>

namespace pinloki
{

/**
 * @brief Helper class to efficiently read a std::ifstream for
 *        the specific needs of pinloki.
 *        The simple explanation is that this is a forward_ifstream.
 *        The member bytes_read() is like ifstream::tellg(), but
 *        returns the number of bytes read thus far (which is the same
 *        as "file position").
 */
class IFStreamReader
{
public:
    IFStreamReader() = default;

    explicit IFStreamReader(const std::string& file_name);
    // For efficiency the file should not have been read yet,
    // i.e. ifs.tellg() == 0.
    explicit IFStreamReader(std::ifstream&& ifs);

    IFStreamReader(IFStreamReader&&) = default;
    IFStreamReader& operator=(IFStreamReader&&) = default;

    /** Is the ifstream open */
    bool is_open() const;

    /** Close the ifstream (the destructor closes too) */
    void close();

    /**
     * @brief advance (or ignore) up to nbytes.
     * @param nbytes
     * @return The number of bytes advanced <= nbytes.
     */
    ssize_t advance(ssize_t nbytes);

    /**
     * @brief advance_for Advance (or ignore) up to nbytes. Blocks
     *                    until `duration` has passed or `nbytes`
     *                    have been read.
     * @param nbytes
     * @return The number of bytes advanced <= nbytes.
     */
    ssize_t advance_for(ssize_t nbytes, mxb::Duration timeout);

    /**
     * @brief  read_n Try to read `nbytes` bytes.
     * @param  pData Buffer that holds at least `nbytes` bytes.
     * @param  `nbytes` Bytes to read.
     * @return true if `nbytes` bytes where read, else the `position`
     *         stays the same.
     */
    bool read_n(char* pData, ssize_t nbytes);

    /**
     * @brief  read_n_for Try to read `n` bytes.
     *                    Blocks until `duration` has elapsed or
     *                    `nbytes` have been read.
     * @param  pData Buffer that holds at least `n` bytes.
     * @param  n Bytes to read.
     * @param  timeout Amount of time to wait for data.
     * @return true if `n` bytes where read, else the `position`
     *         stays the same.
     */
    bool read_n_for(char* pData, ssize_t nbytes, mxb::Duration timeout);

    /**
     * @brief put_back Only one putback can be in effect at any one time.
     *                 The next read must match the size of what was put
     *                 back. `advance()` can not be called when put back is
     *                 in effect.
     *
     *                 Events are read in two parts, first the header
     *                 then the body. If the header is read, but the
     *                 body is not completely in the file, the header is
     *                 "put back" into the stream.
     * @param data
     */
    void put_back(std::vector<char>&& data);

    /**
     * @brief bytes_read Same effect as calling tellg() on the underlying stream
     * @return number of bytes read, i.e. file position.
     */
    ssize_t bytes_read() const;

    /**
     * @brief at_pos Check that the read position is `pos`,
     *               Calls tellg() and asserts on mismatch
     *               in a debug build.
     * @param pos Position to check
     * @return bytes_read() == pos
     */
    bool at_pos(ssize_t pos) const;

private:
    // How many bytes are available to be read.
    void update_in_avail(ssize_t requested);

    std::ifstream     m_ifs;
    ssize_t           m_in_avail = 0;
    ssize_t           m_bytes_read = 0;
    std::vector<char> m_putback_buffer;
};

inline ssize_t IFStreamReader::bytes_read() const
{
    return m_bytes_read;
}
}
