/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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

inline void ensure_at_head(const GWBUF* buf)
{
    mxb_assert(buf->tail != reinterpret_cast<GWBUF*>(0xdeadbeef));
}

inline void ensure_not_empty(const GWBUF* buf)
{
    if (buf)
    {
        mxb_assert(!buf->empty());
    }
}

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
    ensure_at_head(buf);
    ensure_owned(buf);
    return true;
}

#else

inline void ensure_at_head(const GWBUF* head)
{
}

inline void ensure_not_empty(const GWBUF* buf)
{
}

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
    return new GWBUF(size);
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

GWBUF::GWBUF(uint64_t size)
    : sbuf(std::make_shared<SHARED_BUF>(size))
{
#ifdef SS_DEBUG
    owner = RoutingWorker::get_current_id();
#endif
    tail = this;
    start = sbuf->data.data();
    end = start + size;
}

GWBUF::GWBUF(const GWBUF& rhs)
    : start(rhs.start)
    , end(rhs.end)
    , sbuf(rhs.sbuf)
    , hints(rhs.hints)
    , gwbuf_type(rhs.gwbuf_type)
    , id(rhs.id)
{
#ifdef SS_DEBUG
    owner = RoutingWorker::get_current_id();
#endif
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

GWBUF* gwbuf_clone(GWBUF* buf)
{
    auto* rval = new GWBUF(*buf);
    rval->tail = rval;
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
    validate_buffer(buf);
    return gwbuf_deep_clone_portion(buf, gwbuf_length(buf));
}

GWBUF* gwbuf_split(GWBUF** buf, size_t length)
{
    validate_buffer(*buf);
    GWBUF* head = NULL;

    if (length > 0 && buf && *buf)
    {
        GWBUF* buffer = *buf;
        GWBUF* orig_tail = buffer->tail;
        head = buffer;
        ensure_owned(buffer);

        /** Handle complete buffers */
        while (buffer && length && length >= gwbuf_link_length(buffer))
        {
            length -= gwbuf_link_length(buffer);
            head->tail = buffer;
            buffer = buffer->next;
        }

        /** Some data is left in the original buffer */
        if (buffer)
        {
            /** We're splitting a chain of buffers */
            if (head->tail != orig_tail)
            {
                /** Make sure the original buffer's tail points to the right place */
                buffer->tail = orig_tail;

                /** Remove the pointer to the original buffer */
                head->tail->next = NULL;
            }

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
        *buf = (*buf)->next;
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
    // This function is used such that 'head' should take ownership of 'tail'. Since 'append' now copies
    // the contents, free the tail.
    // In the long run, this function should not be required. The most common use is in reading from a
    // network socket. That should be optimized to write directly to the buffer.
    if (head)
    {
        head->append(tail);
        gwbuf_free(tail);
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
    auto& obj = sbuf->classifier_data;
    mxb_assert(obj.data == nullptr && obj.deleter == nullptr);
    mxb_assert(!new_data || deleter);   // If data is given, a deleter must also be set.
    obj.data = new_data;
    obj.deleter = deleter;
}

void* GWBUF::get_classifier_data() const
{
    return sbuf->classifier_data.data;
}

void GWBUF::append(const uint8_t* new_data, uint64_t n_bytes)
{
    auto old_len = length();
    auto new_len = old_len + n_bytes;

    if (sbuf.unique())
    {
        auto& bufdata = sbuf->data;
        auto bytes_consumed = start - bufdata.data();

        auto vec_size_required = bytes_consumed + new_len;
        if (vec_size_required > bufdata.size())
        {
            // Resize the vector so that the additional data can be easily added. The vector may reallocate,
            // so recalculate pointers.
            bufdata.resize(vec_size_required);
            start = bufdata.data() + bytes_consumed;
            end = start + old_len;
        }

        // This may overwrite trimmed data.
        memcpy(end, new_data, n_bytes);
        end += n_bytes;
    }
    else
    {
        // If called for a shared (shallow-cloned) buffer, make a new copy of the underlying data. The custom
        // data is not copied, as it does not have a copy-function.
        // TODO: think if custom data should be copied.
        auto new_sbuf = std::make_shared<SHARED_BUF>(new_len);
        auto* new_vec_begin = new_sbuf->data.data();
        memcpy(new_vec_begin, start, old_len);
        memcpy(new_vec_begin + old_len, new_data, n_bytes);
        sbuf = move(new_sbuf);
        start = sbuf->data.data();
        end = start + new_len;
    }
}

void GWBUF::append(GWBUF* buffer)
{
    append(buffer->data(), buffer->length());
}

uint8_t* GWBUF::consume(uint64_t bytes)
{
    // TODO: attempting to consume more than 'length' should be an error.
    // Avoid reallocations and copies here, as the GWBUF is typically freed after consume anyways.
    if (bytes > length())
    {
        mxb_assert(!true);
        start = end;
    }
    else
    {
        start += bytes;
    }
    return start;
}

void GWBUF::rtrim(uint64_t bytes)
{
    // TODO: attempting to trim more than 'length' should be an error.
    if (bytes > length())
    {
        mxb_assert(!true);
        end = start;
    }
    else
    {
        end -= bytes;
    }
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

size_t gwbuf_copy_data(const GWBUF* buffer, size_t offset, size_t bytes, uint8_t* dest)
{
    uint32_t buflen;

    /** Skip unrelated buffers */
    if (buffer && offset && offset >= (buflen = gwbuf_link_length(buffer)))
    {
        mxb_assert(buffer->owner == RoutingWorker::get_current_id());
        offset -= buflen;
        buffer = nullptr;
    }

    size_t bytes_read = 0;
    if (buffer)
    {
        mxb_assert(buffer->owner == RoutingWorker::get_current_id());
        uint8_t* ptr = (uint8_t*) GWBUF_DATA(buffer) + offset;
        uint32_t bytes_left = gwbuf_link_length(buffer) - offset;

        /** Data is in one buffer */
        if (bytes_left >= bytes)
        {
            memcpy(dest, ptr, bytes);
            bytes_read = bytes;
        }
        else
        {
            /** Data is spread across multiple buffers */
            memcpy(dest, ptr, bytes_left);
            bytes -= bytes_left;
            dest += bytes_left;
            bytes_read += bytes_left;
        }
    }

    return bytes_read;
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
