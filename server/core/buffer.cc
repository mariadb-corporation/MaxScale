/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/buffer.hh>

#include <cstdlib>
#include <sstream>
#include <utility>

#include <maxbase/assert.hh>
#include <maxbase/hexdump.hh>
#include <maxscale/config.hh>
#include <maxscale/hint.hh>
#include <maxscale/routingworker.hh>

using mxs::RoutingWorker;
using std::move;

GWBUF::GWBUF(size_t size)
    : m_sbuf(std::make_shared<SHARED_BUF>(size))
    , m_start(m_sbuf->buf_start.get())
    , m_end(m_start + size)
{
}

GWBUF::GWBUF(const uint8_t* data, size_t datasize)
    : GWBUF(datasize)
{
    memcpy(m_start, data, datasize);
}

GWBUF GWBUF::shallow_clone() const
{
    return GWBUF(*this);
}

GWBUF GWBUF::deep_clone() const
{
    GWBUF rval = shallow_clone();
    rval.ensure_unique();
    return rval;
}

GWBUF GWBUF::split(uint64_t n_bytes)
{
    auto len = length();
    GWBUF rval;
    // Splitting more than is available is an error.
    mxb_assert(n_bytes <= len);

    if (n_bytes == 0)
    {
        // Do nothing, return empty buffer.
    }
    else if (n_bytes == len)
    {
        rval = std::move(*this);
        clear();
    }
    else
    {
        // Shallow clone buffer, then consume and trim accordingly. As the data is split, many of the
        // fields revert to default values on both fragments.
        rval.m_sbuf = m_sbuf;
        rval.m_start = m_start;
        rval.m_end = m_end;

        m_hints.clear();
        m_type = TYPE_UNDEFINED;
        m_id = 0;
        m_protocol_info.reset();

        consume(n_bytes);
        rval.rtrim(len - n_bytes);
    }
    return rval;
}

int GWBUF::compare(const GWBUF& rhs) const
{
    size_t llen = length();
    size_t rlen = rhs.length();

    return (llen == rlen) ? memcmp(data(), rhs.data(), llen) : ((llen > rlen) ? 1 : -1);
}

void GWBUF::set_type(Type type)
{
    m_type |= type;
}

void GWBUF::append(const uint8_t* new_data, uint64_t n_bytes)
{
    if (n_bytes > 0)
    {
        auto [ptr, _1] = prepare_to_write(n_bytes);
        memcpy(ptr, new_data, n_bytes);
        write_complete(n_bytes);
    }
}

std::tuple<uint8_t*, size_t> GWBUF::prepare_to_write(uint64_t n_bytes)
{
    auto old_len = length();
    auto new_len = old_len + n_bytes;

    auto clone_sbuf = [this, old_len](size_t alloc_size) {
        auto new_sbuf = std::make_shared<SHARED_BUF>(alloc_size);
        auto* new_buf_start = new_sbuf->buf_start.get();
        if (old_len > 0)
        {
            memcpy(new_buf_start, m_start, old_len);
        }
        m_sbuf = move(new_sbuf);
        m_start = new_buf_start;
        m_end = m_start + old_len;
    };

    if (m_sbuf.unique())
    {
        if (m_sbuf->buf_end - m_end >= (int64_t)n_bytes)
        {
            // Have enough space at end of buffer.
        }
        else if (m_sbuf->size() >= new_len)
        {
            // Did not have enough space at the end, but the allocated space is enough. This can happen if
            // most of the buffer has been consumed. Make space by moving data.
            const auto buf_start = m_sbuf->buf_start.get();
            memmove(buf_start, m_start, old_len);
            m_start = buf_start;
            m_end = m_start + old_len;
        }
        else
        {
            // Have to reallocate the shared buffer. At least double the previous size to handle future
            // writes.
            auto alloc_size = std::max(new_len, 2 * m_sbuf->size());
            clone_sbuf(alloc_size);
        }
    }
    else
    {
        // If called for a shared (shallow-cloned) buffer, make a new copy of the underlying data.
        // Also ends up here if the shared ptr is null.
        clone_sbuf(new_len);
    }
    return {m_end, m_sbuf->buf_end - m_end};
}

void GWBUF::append(const GWBUF& buffer)
{
    append(buffer.data(), buffer.length());
}

uint8_t* GWBUF::consume(uint64_t bytes)
{
    // Consuming more than 'length' is an error.
    mxb_assert(bytes <= length());
    m_start += bytes;
    return m_start;
}

void GWBUF::rtrim(uint64_t bytes)
{
    // Trimming more than 'length' is an error.
    mxb_assert(bytes <= length());
    m_end -= bytes;
}

void GWBUF::clear()
{
    m_sbuf.reset();
    m_protocol_info.reset();
    m_hints.clear();
    m_start = nullptr;
    m_end = nullptr;
    m_id = 0;
    m_type = TYPE_UNDEFINED;
}

void GWBUF::reset()
{
    if (m_sbuf)
    {
        m_start = m_sbuf->buf_start.get();
        m_end = m_start;
    }
    else
    {
        mxb_assert(m_start == nullptr && m_end == nullptr);
    }

    m_hints.clear();
    m_protocol_info.reset();
    m_type = TYPE_UNDEFINED;
    m_id = 0;
}

void GWBUF::ensure_unique()
{
    prepare_to_write(0);
    mxb_assert(!m_sbuf || m_sbuf.unique());
}

bool GWBUF::is_unique() const
{
    return m_sbuf.unique();
}

size_t GWBUF::capacity() const
{
    return m_sbuf ? m_sbuf->size() : 0;
}

void GWBUF::set_id(uint32_t new_id)
{
    mxb_assert(m_id == 0);
    m_id = new_id;
}

void GWBUF::merge_front(GWBUF&& buffer)
{
    if (!buffer.empty())
    {
        buffer.append(*this);
        // TODO: can be improved with similar logic as in prepare_to_write.
        *this = std::move(buffer);
    }
}

void GWBUF::merge_back(GWBUF&& buffer)
{
    if (!buffer.empty())
    {
        if (empty())
        {
            *this = std::move(buffer);
        }
        else
        {
            append(buffer);
        }
    }
}

size_t GWBUF::copy_data(size_t offset, size_t n_bytes, uint8_t* dst) const
{
    auto len = length();
    size_t copied_bytes = 0;
    if (offset < len)
    {
        auto bytes_left = len - offset;
        copied_bytes = std::min(bytes_left, n_bytes);
        memcpy(dst, m_start + offset, copied_bytes);
    }
    return copied_bytes;
}

const uint8_t& GWBUF::operator[](size_t ind) const
{
    const auto* ptr = m_start + ind;
    mxb_assert(m_start && ptr < m_end);
    return *ptr;
}

size_t GWBUF::varying_size() const
{
    size_t rv = 0;

    if (m_sbuf)
    {
        rv += sizeof(*m_sbuf);
        rv += m_sbuf->size() / m_sbuf.use_count();
    }

    if (m_protocol_info)
    {
        rv += m_protocol_info->size() / m_protocol_info.use_count();
    }

    return rv;
}

size_t GWBUF::runtime_size() const
{
    return sizeof(*this) + varying_size();
}

SHARED_BUF::SHARED_BUF(size_t len)
    : buf_start(new uint8_t[len])   // Don't use make_unique here, it zero-inits the buffer
    , buf_end(buf_start.get() + len)
{
}
