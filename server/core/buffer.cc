/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
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

#if defined (SS_DEBUG)

inline void ensure_owned(const GWBUF* buf)
{
    // TODO: Currently not possible to know whether manually triggered
    // TODO: rebalance has taken place.
#ifdef CAN_DETECT_WHETHER_REBALANCE_IN_PROCESS
    if (config_get_global_options()->rebalance_threshold == 0)
    {
        // TODO: If rebalancing occurs, then if a session has been moved while a
        // TODO: router session has kept a reference to a GWBUF, then buf->owner
        // TODO: will not be correct and the assertion would fire. Currently there
        // TODO: is no simple way to track those GWBUFS down in order to change the
        // TODO: owner. So for the time being we don't check the owner if rebalancing
        // TODO: is active.
        mxb_assert(buf->owner == RoutingWorker::get_current());
    }
#endif
}

inline bool validate_buffer(const GWBUF* buf)
{
    mxb_assert(buf);
    ensure_owned(buf);
    return true;
}

#else
inline void ensure_owned(const GWBUF* head)
{
}

inline bool validate_buffer(const GWBUF* head)
{
    return true;
}
#endif

/**
 * Allocate a new gateway buffer structure of size bytes.
 *
 * For now we allocate memory directly from malloc for buffer the management
 * structure and the actual data buffer itself. We may swap at a future date
 * to a more efficient mechanism.
 *
 * @param       size The size in bytes of the data area required
 * @return      Pointer to the buffer structure or NULL if memory could not
 *              be allocated.
 */
GWBUF* gwbuf_alloc(unsigned int size)
{
    mxb_assert(size > 0);
    auto rval = new GWBUF(size);
    return rval;
}

GWBUF::GWBUF()
{
#ifdef SS_DEBUG
    m_owner = mxb::Worker::get_current();
#endif
}

GWBUF::GWBUF(size_t size)
    : GWBUF()
{
    m_sbuf = std::make_shared<SHARED_BUF>(size);
    m_start = m_sbuf->buf_start.get();
    m_end = m_start + size;
}

GWBUF::GWBUF(const uint8_t* data, size_t datasize)
    : GWBUF()
{
    append(data, datasize);
}

GWBUF GWBUF::shallow_clone() const
{
    GWBUF rval;
    rval.clone_helper(*this);

    rval.m_start = m_start;
    rval.m_end = m_end;
    rval.m_sbuf = m_sbuf;
    return rval;
}

GWBUF::GWBUF(GWBUF&& rhs) noexcept
    : hints(std::move(rhs.hints))
    , m_sbuf(std::move(rhs.m_sbuf))
    , m_protocol_info(std::move(rhs.m_protocol_info))
    , m_start(std::exchange(rhs.m_start, nullptr))
    , m_end(std::exchange(rhs.m_end, nullptr))
    , m_id(std::exchange(rhs.m_id, 0))
    , m_type(std::exchange(rhs.m_type, TYPE_UNDEFINED))
#ifdef SS_DEBUG
    , m_owner(RoutingWorker::get_current())
#endif
{
}

GWBUF& GWBUF::operator=(GWBUF&& rhs) noexcept
{
    if (this != &rhs)
    {
        move_helper(move(rhs));
        mxb_assert(rhs.empty());
    }
    return *this;
}

void GWBUF::move_helper(GWBUF&& rhs) noexcept
{
    using std::exchange;
    m_start = exchange(rhs.m_start, nullptr);
    m_end = exchange(rhs.m_end, nullptr);
    m_type = exchange(rhs.m_type, TYPE_UNDEFINED);
    m_id = exchange(rhs.m_id, 0);

    hints = move(rhs.hints);
    m_protocol_info = std::move(rhs.m_protocol_info);
    m_sbuf = move(rhs.m_sbuf);
}

GWBUF GWBUF::deep_clone() const
{
    GWBUF rval;
    rval.clone_helper(*this);
    rval.append(*this);
    return rval;
}

/**
 * Free a list of gateway buffers
 *
 * @param buf The head of the list of buffers to free
 */
void gwbuf_free(GWBUF* buf)
{
    delete buf;
}

void GWBUF::clone_helper(const GWBUF& other)
{
    hints = other.hints;
    m_type = other.m_type;
    m_id = other.m_id;
    m_protocol_info = other.m_protocol_info;
}

GWBUF* gwbuf_clone_shallow(GWBUF* buf)
{
    return mxs::gwbuf_to_gwbufptr(buf->shallow_clone());
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
        rval.move_helper(move(*this));
    }
    else
    {
        // Shallow clone buffer, then consume and trim accordingly. As the data is split, many of the
        // fields revert to default values on both fragments.
        rval.m_sbuf = m_sbuf;
        rval.m_start = m_start;
        rval.m_end = m_end;

        hints.clear();
        m_type = TYPE_UNDEFINED;
        m_id = 0;
        m_protocol_info.reset();

        consume(n_bytes);
        rval.rtrim(len - n_bytes);
    }
    return rval;
}

GWBUF* gwbuf_split(GWBUF** buf, size_t length)
{
    mxb_assert(buf && *buf);
    GWBUF* rval = nullptr;

    if (length > 0)
    {
        GWBUF* source = *buf;
        auto splitted = source->split(length);
        rval = new GWBUF(move(splitted));

        if (source->empty())
        {
            delete source;
            *buf = nullptr;
        }
    }
    return rval;
}

int gwbuf_compare(const GWBUF* lhs, const GWBUF* rhs)
{
    return lhs->compare(*rhs);
}

int GWBUF::compare(const GWBUF& rhs) const
{
    size_t llen = length();
    size_t rlen = rhs.length();

    return (llen == rlen) ? memcmp(data(), rhs.data(), llen) : ((llen > rlen) ? 1 : -1);
}

GWBUF* gwbuf_append(GWBUF* head, GWBUF* tail)
{
    // This function is used such that 'head' should take ownership of 'tail'.
    if (head)
    {
        if (tail)
        {
            head->merge_back(move(*tail));
            delete tail;
        }
        return head;
    }
    else
    {
        return tail;
    }
}

GWBUF* gwbuf_consume(GWBUF* head, uint64_t length)
{
    // TODO: Avoid calling this function.
    auto rval = head;
    head->consume(length);
    if (head->empty())
    {
        gwbuf_free(head);
        rval = nullptr;
    }
    return rval;
}

unsigned int gwbuf_length(const GWBUF* head)
{
    return head->length();
}

void GWBUF::set_type(Type type)
{
    m_type |= type;
}

void GWBUF::set_protocol_info(std::shared_ptr<ProtocolInfo> new_info)
{
    m_protocol_info = std::move(new_info);
}

const std::shared_ptr<GWBUF::ProtocolInfo>& GWBUF::get_protocol_info() const
{
    return m_protocol_info;
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
    move_helper(GWBUF());
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

    hints.clear();
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

#ifdef SS_DEBUG
void GWBUF::set_owner(mxb::Worker* owner)
{
    m_owner = owner;
}
#endif

void GWBUF::merge_front(GWBUF&& buffer)
{
    if (!buffer.empty())
    {
        buffer.append(*this);
        // TODO: can be improved with similar logic as in prepare_to_write.
        move_helper(move(buffer));
    }
}

void GWBUF::merge_back(GWBUF&& buffer)
{
    if (!buffer.empty())
    {
        if (empty())
        {
            move_helper(move(buffer));
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

static std::string dump_one_buffer(GWBUF* buffer)
{
    ensure_owned(buffer);
    std::string rval;
    int len = gwbuf_link_length(buffer);
    uint8_t* data = GWBUF_DATA(buffer);

    while (len > 0)
    {
        // Process the buffer in 40 byte chunks
        int n = std::min(40, len);
        char output[n * 2 + 1];
        mxs::bin2hex(data, n, output);
        char* ptr = output;

        while (ptr < output + n * 2)
        {
            rval.append(ptr, 2);
            rval += " ";
            ptr += 2;
        }
        len -= n;
        data += n;
        rval += "\n";
    }

    return rval;
}

void gwbuf_hexdump(GWBUF* buffer, int log_level)
{
    std::stringstream ss;

    ss << "Buffer " << buffer << ":\n";
    ss << dump_one_buffer(buffer);

    int n = ss.str().length();

    if (n > 1024)
    {
        n = 1024;
    }

    MXB_LOG_MESSAGE(log_level, "%.*s", n, ss.str().c_str());
}

void gwbuf_hexdump_pretty(GWBUF* buffer, int log_level)
{
    mxs::Buffer buf(buffer);
    buf.hexdump_pretty(log_level);
    buf.release();
}

void mxs::Buffer::hexdump(int log_level) const
{
    return gwbuf_hexdump(m_pBuffer, log_level);
}

void mxs::Buffer::hexdump_pretty(int log_level) const
{
    MXB_LOG_MESSAGE(log_level, "%s", mxb::hexdump(data(), length()).c_str());
}

uint8_t* maxscale::Buffer::data()
{
    return GWBUF_DATA(m_pBuffer);
}

const uint8_t* maxscale::Buffer::data() const
{
    return GWBUF_DATA(m_pBuffer);
}

SHARED_BUF::SHARED_BUF(size_t len)
    : buf_start(new uint8_t[len])   // Don't use make_unique here, it zero-inits the buffer
    , buf_end(buf_start.get() + len)
{
}

GWBUF* mxs::gwbuf_to_gwbufptr(GWBUF&& buffer)
{
    return new GWBUF(std::move(buffer));
}

GWBUF mxs::gwbufptr_to_gwbuf(GWBUF* buffer)
{
    GWBUF rval(std::move(*buffer));
    delete buffer;
    return rval;
}
