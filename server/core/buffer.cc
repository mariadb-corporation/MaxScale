/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/buffer.hh>

#include <cstdlib>
#include <sstream>

#include <maxbase/alloc.h>
#include <maxbase/assert.h>
#include <maxscale/config.hh>
#include <maxscale/hint.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/modutil.hh>

using mxs::RoutingWorker;
using std::move;

namespace
{
std::string extract_sql_real(const GWBUF* pBuf)
{
    mxb_assert(pBuf != nullptr);

    std::string rval;
    uint8_t cmd = mxs_mysql_get_command(pBuf);

    if (cmd == MXS_COM_QUERY || cmd == MXS_COM_STMT_PREPARE)
    {
        // Skip the packet header and the command byte
        size_t header_len = MYSQL_HEADER_LEN + 1;
        size_t length = gwbuf_length(pBuf) - header_len;
        rval.resize(length);
        char* pCopy_from = (char*) GWBUF_DATA(pBuf) + header_len;
        char* pCopy_to = &rval.front();
        memcpy(pCopy_to, pCopy_from, length);
    }

    return rval;
}
}

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
        mxb_assert(buf->owner == RoutingWorker::get_current_id());
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
    rval->write_complete(size);     // Callers expect the end-pointer to point to buffer end
    return rval;
}

const std::string& GWBUF::get_sql() const
{
    if (m_sql.empty())
    {
        m_sql = extract_sql_real(this);
    }

    return m_sql;
}

const std::string& GWBUF::get_canonical() const
{
    if (m_canonical.empty())
    {
        m_canonical = get_sql();
        maxsimd::get_canonical(&m_canonical, &m_markers);
    }

    return m_canonical;
}

GWBUF::GWBUF()
{
#ifdef SS_DEBUG
    owner = RoutingWorker::get_current_id();
#endif
}

GWBUF::GWBUF(size_t reserve_size)
    : GWBUF()
{
    m_sbuf = std::make_shared<SHARED_BUF>(reserve_size);
    start = m_sbuf->buf_start.get();
    end = start;
}

GWBUF::GWBUF(GWBUF&& rhs) noexcept
    : GWBUF()
{
    move_helper(move(rhs));
}

GWBUF& GWBUF::operator=(GWBUF&& rhs) noexcept
{
    if (this != &rhs)
    {
        move_helper(move(rhs));
    }
    return *this;
}

void GWBUF::move_helper(GWBUF&& rhs) noexcept
{
    using std::exchange;
    start = exchange(rhs.start, nullptr);
    end = exchange(rhs.end, nullptr);
    gwbuf_type = exchange(rhs.gwbuf_type, GWBUF_TYPE_UNDEFINED);
    id = exchange(rhs.id, 0);

    hints = move(rhs.hints);
    m_sbuf = move(rhs.m_sbuf);
    m_sql = move(rhs.m_sql);
    m_canonical = move(rhs.m_canonical);
    m_markers = move(rhs.m_markers);
}

/**
 * Allocate a new gateway buffer structure of size bytes and load with data.
 *
 * @param       size    The size in bytes of the data area required
 * @param       data    Pointer to the data (size bytes) to be loaded
 * @return      Pointer to the buffer structure or NULL if memory could not
 *              be allocated.
 */
GWBUF* gwbuf_alloc_and_load(unsigned int size, const void* data)
{
    mxb_assert(size > 0);
    GWBUF* rval = gwbuf_alloc(size);

    if (rval)
    {
        memcpy(GWBUF_DATA(rval), data, size);
    }

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
    gwbuf_type = other.gwbuf_type;
    id = other.id;

    m_sql = other.m_sql;
    m_canonical = other.m_canonical;
    // No need to copy 'markers'.
}

GWBUF GWBUF::clone_shallow() const
{
    GWBUF rval;
    rval.clone_helper(*this);

    rval.start = start;
    rval.end = end;
    rval.m_sbuf = m_sbuf;
    return rval;
}

GWBUF GWBUF::clone_deep() const
{
    auto len = length();
    GWBUF rval(len);
    rval.clone_helper(*this);

    memcpy(rval.start, start, len);
    rval.write_complete(len);
    // TODO: clone BufferObject
    return rval;
}

GWBUF* gwbuf_clone_shallow(GWBUF* buf)
{
    auto* rval = new GWBUF();
    *rval = buf->clone_shallow();
    return rval;
}

static GWBUF* gwbuf_deep_clone_portion(const GWBUF* buf, size_t length)
{
    ensure_owned(buf);
    GWBUF* rval = NULL;

    if (buf)
    {
        rval = gwbuf_alloc(length);

        if (rval && gwbuf_copy_data(buf, 0, length, GWBUF_DATA(rval)) == length)
        {
            // The copying of the type is done to retain the type characteristic of the buffer without
            // having a link the orginal data or parsing info.
            rval->gwbuf_type = buf->gwbuf_type;
            rval->id = buf->id;
        }
        else
        {
            gwbuf_free(rval);
            rval = NULL;
        }
    }

    return rval;
}

GWBUF* gwbuf_deep_clone(const GWBUF* buf)
{
    auto rval = new GWBUF();
    *rval = buf->clone_deep();
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
        rval.move_helper(move(*this));
    }
    else
    {
        // Shallow clone buffer, then consume and trim accordingly.
        rval = clone_shallow();
        consume(n_bytes);
        rval.rtrim(len - n_bytes);
    }
    return rval;
}

GWBUF* gwbuf_split(GWBUF** buf, size_t length)
{
    validate_buffer(*buf);
    GWBUF* head = NULL;

    if (length > 0 && buf && *buf)
    {
        GWBUF* buffer = *buf;
        GWBUF* orig_tail = nullptr;
        head = buffer;
        ensure_owned(buffer);

        /** Handle complete buffers */
        while (buffer && length && length >= gwbuf_link_length(buffer))
        {
            length -= gwbuf_link_length(buffer);
            buffer = nullptr;
        }

        /** Some data is left in the original buffer */
        if (buffer)
        {
            if (length > 0)
            {
                mxb_assert(gwbuf_link_length(buffer) > length);
                GWBUF* partial = gwbuf_deep_clone_portion(buffer, length);

                /** If the head points to the original head of the buffer chain
                 * and we are splitting a contiguous buffer, we only need to return
                 * the partial clone of the first buffer. If we are splitting multiple
                 * buffers, we need to append them to the full buffers. */
                head = head == buffer ? partial : gwbuf_append(head, partial);

                buffer = gwbuf_consume(buffer, length);
            }
        }

        *buf = buffer;
    }

    return head;
}

/**
 * Get a byte from a GWBUF at a particular offset. Intended to be use like:
 *
 *     GWBUF *buf = ...;
 *     size_t offset = 0;
 *     uint8_t c;
 *
 *     while (gwbuf_get_byte(&buf, &offset, &c))
 *     {
 *         printf("%c", c);
 *     }
 *
 * @param buf     Pointer to pointer to GWBUF. The GWBUF pointed to may be adjusted
 *                as a result of the call.
 * @param offset  Pointer to variable containing the offset. Value of variable will
 *                incremented as a result of the call.
 * @param b       Pointer to variable that upon successful return will contain the
 *                next byte.
 *
 * @return True, if offset refers to a byte in the GWBUF.
 */
static inline bool gwbuf_get_byte(const GWBUF** buf, size_t* offset, uint8_t* b)
{
    bool rv = false;

    // Ignore NULL buffer and walk past empty or too short buffers.
    while (*buf && (gwbuf_link_length(*buf) <= *offset))
    {
        mxb_assert((*buf)->owner == RoutingWorker::get_current_id());
        *offset -= gwbuf_link_length(*buf);
        *buf = nullptr;
    }

    mxb_assert(!*buf || (gwbuf_link_length(*buf) > *offset));

    if (*buf)
    {
        mxb_assert((*buf)->owner == RoutingWorker::get_current_id());
        *b = *(GWBUF_DATA(*buf) + *offset);
        *offset += 1;

        rv = true;
    }

    return rv;
}

int gwbuf_compare(const GWBUF* lhs, const GWBUF* rhs)
{
    validate_buffer(lhs);
    validate_buffer(rhs);

    int rv;

    size_t llen = gwbuf_length(lhs);
    size_t rlen = gwbuf_length(rhs);

    if (llen < rlen)
    {
        rv = -1;
    }
    else if (rlen < llen)
    {
        rv = 1;
    }
    else
    {
        mxb_assert(llen == rlen);

        rv = 0;
        size_t i = 0;
        size_t loffset = 0;
        size_t roffset = 0;

        while ((rv == 0) && (i < llen))
        {
            uint8_t lc = 0;
            uint8_t rc = 0;

            MXB_AT_DEBUG(bool rv1 = ) gwbuf_get_byte(&lhs, &loffset, &lc);
            MXB_AT_DEBUG(bool rv2 = ) gwbuf_get_byte(&rhs, &roffset, &rc);

            mxb_assert(rv1 && rv2);

            rv = (int)lc - (int)rc;

            ++i;
        }

        if (rv < 0)
        {
            rv = -1;
        }
        else if (rv > 0)
        {
            rv = 1;
        }
    }

    return rv;
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

GWBUF* gwbuf_rtrim(GWBUF* head, uint64_t n_bytes)
{
    head->rtrim(n_bytes);
    return head->empty() ? nullptr : head;
}

void gwbuf_set_type(GWBUF* buf, uint32_t type)
{
    buf->gwbuf_type |= type;
}

void GWBUF::set_classifier_data(void* new_data, void (* deleter)(void*))
{
    auto& obj = m_sbuf->classifier_data;
    mxb_assert(obj.data == nullptr && obj.deleter == nullptr);
    mxb_assert(!new_data || deleter);   // If data is given, a deleter must also be set.
    obj.data = new_data;
    obj.deleter = deleter;
}

void* GWBUF::get_classifier_data() const
{
    return m_sbuf->classifier_data.data;
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

    auto clone_sbuf = [this, old_len](bool swap_cl_data, size_t alloc_size) {
            auto new_sbuf = std::make_shared<SHARED_BUF>(alloc_size);

            auto* new_buf_start = new_sbuf->buf_start.get();
            memcpy(new_buf_start, start, old_len);
            if (swap_cl_data)
            {
                std::swap(new_sbuf->classifier_data, m_sbuf->classifier_data);
            }

            m_sbuf = move(new_sbuf);
            start = new_buf_start;
            end = start + old_len;
        };

    if (m_sbuf.unique())
    {
        if (m_sbuf->buf_end - end >= (int64_t)n_bytes)
        {
            // Have enough space at end of buffer.
        }
        else if (m_sbuf->size() >= new_len)
        {
            // Did not have enough space at the end, but the allocated space is enough. This can happen if
            // most of the buffer has been consumed. Make space by moving data.
            const auto buf_start = m_sbuf->buf_start.get();
            memmove(buf_start, start, old_len);
            start = buf_start;
            end = start + old_len;
        }
        else
        {
            // Have to reallocate the shared buffer. At least double the previous size to handle future
            // writes.
            auto alloc_size = std::max(new_len, 2 * m_sbuf->size());
            clone_sbuf(true, alloc_size);
        }
    }
    else
    {
        // If called for a shared (shallow-cloned) buffer, make a new copy of the underlying data. The custom
        // data is not copied, as it does not have a copy-function.
        // TODO: think if custom data should be copied.
        // Also ends up here if the shared ptr is null.
        clone_sbuf(false, new_len);
    }
    return {end, m_sbuf->buf_end - end};
}

void GWBUF::append(const GWBUF& buffer)
{
    append(buffer.data(), buffer.length());
}

uint8_t* GWBUF::consume(uint64_t bytes)
{
    // Consuming more than 'length' is an error.
    mxb_assert(bytes <= length());
    start += bytes;
    return start;
}

void GWBUF::rtrim(uint64_t bytes)
{
    // Trimming more than 'length' is an error.
    mxb_assert(bytes <= length());
    end -= bytes;
}

void GWBUF::clear()
{
    move_helper(GWBUF());
}

void gwbuf_set_id(GWBUF* buffer, uint32_t id)
{
    validate_buffer(buffer);
    mxb_assert(buffer->id == 0);
    buffer->id = id;
}

uint32_t gwbuf_get_id(GWBUF* buffer)
{
    validate_buffer(buffer);
    return buffer->id;
}

GWBUF* gwbuf_make_contiguous(GWBUF* orig)
{
    // Already contiguous
    return orig;
}

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
        memcpy(dst, start + offset, copied_bytes);
    }
    return copied_bytes;
}

size_t gwbuf_copy_data(const GWBUF* buffer, size_t offset, size_t bytes, uint8_t* dest)
{
    return buffer->copy_data(offset, bytes, dest);
}

uint8_t* gwbuf_byte_pointer(GWBUF* buffer, size_t offset)
{
    uint8_t* rval = nullptr;
    if (buffer && buffer->length() > offset)
    {
        rval = buffer->start + offset;
    }
    return rval;
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
        int n = MXS_MIN(40, len);
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

    MXS_LOG_MESSAGE(log_level, "%.*s", n, ss.str().c_str());
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
    constexpr const char as_hex[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string result = "\n";
    std::string hexed;
    std::string readable;
    auto it = begin();

    while (it != end())
    {
        for (int i = 0; i < 16 && it != end(); i++)
        {
            uint8_t c = *it;
            hexed += as_hex[c >> 4];
            hexed += as_hex[c & 0x0f];
            hexed += ' ';
            readable += isprint(c) && (!isspace(c) || c == ' ') ? (char)c : '.';
            ++it;
        }

        if (readable.length() < 16)
        {
            hexed.append(48 - hexed.length(), ' ');
            readable.append(16 - readable.length(), ' ');
        }

        mxb_assert(hexed.length() == readable.length() * 3);
        result += hexed.substr(0, 24);
        result += "  ";
        result += hexed.substr(24);
        result += "  ";
        result += readable;
        result += '\n';

        hexed.clear();
        readable.clear();
    }

    MXS_LOG_MESSAGE(log_level, "%s", result.c_str());
}

uint8_t* maxscale::Buffer::data()
{
    return GWBUF_DATA(m_pBuffer);
}

const uint8_t* maxscale::Buffer::data() const
{
    return GWBUF_DATA(m_pBuffer);
}

BufferObject::~BufferObject()
{
    if (deleter)
    {
        deleter(data);
    }
}

SHARED_BUF::SHARED_BUF(size_t len)
    : buf_start(std::make_unique<uint8_t[]>(len))
    , buf_end(buf_start.get() + len)
{
}
