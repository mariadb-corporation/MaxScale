/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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
#include <maxscale/utils.h>
#include <maxscale/modutil.hh>

using mxs::RoutingWorker;

struct buffer_object_t
{
    bufobj_id_t      bo_id;
    void*            bo_data;
    void             (* bo_donefun_fp)(void*);
    buffer_object_t* bo_next;
};

static void             gwbuf_free_one(GWBUF* buf);
static buffer_object_t* gwbuf_remove_buffer_object(GWBUF* buf, buffer_object_t* bufobj);

#if defined (SS_DEBUG)
inline void invalidate_tail_pointers(GWBUF* head)
{
    if (head && head->next)
    {
        GWBUF* link = head->next;
        while (link != head->tail)
        {
            link->tail = reinterpret_cast<GWBUF*>(0xdeadbeef);
            link = link->next;
        }
    }
}

inline void ensure_at_head(const GWBUF* buf)
{
    mxb_assert(buf->tail != reinterpret_cast<GWBUF*>(0xdeadbeef));
}

inline void ensure_not_empty(const GWBUF* buf)
{
    for (; buf; buf = buf->next)
    {
        mxb_assert(!gwbuf_link_empty(buf));
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
    ensure_not_empty(buf);
    ensure_at_head(buf);
    ensure_owned(buf);
    return true;
}

#else
inline void invalidate_tail_pointers(GWBUF* head)
{
}

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
        m_sql = maxscale::extract_sql_real(this);
    }

    return m_sql;
}

GWBUF::GWBUF(uint64_t size)
    : sbuf(std::make_shared<SHARED_BUF>(size))
{
#ifdef SS_DEBUG
    owner = RoutingWorker::get_current_id();
#endif
    tail = this;
    start = &sbuf->data.front();
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
    mxb_assert(!buf || validate_buffer(buf));

    while (buf)
    {
        GWBUF* nextbuf = buf->next;
        gwbuf_free_one(buf);
        buf = nextbuf;
    }
}

/**
 * Free a single gateway buffer
 *
 * @param buf The buffer to free
 */
static void gwbuf_free_one(GWBUF* buf)
{
    ensure_owned(buf);

    delete buf;
}

SHARED_BUF::~SHARED_BUF()
{
    buffer_object_t* bo = bufobj;

    while (bo != NULL)
    {
        auto next = bo->bo_next;
        bo->bo_donefun_fp(bufobj->bo_data);
        MXS_FREE(bo);
        bo = next;
    }
}

/**
 * Increment the usage count of a gateway buffer. This gets a new
 * GWBUF structure that shares the actual data with the existing
 * GWBUF structure but allows for the data copy to be avoided and
 * also for each GWBUF to point to different portions of the same
 * SHARED_BUF.
 *
 * @param buf The buffer to use
 * @return A new GWBUF structure
 */
static GWBUF* gwbuf_clone_one(GWBUF* buf)
{
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    return new GWBUF(*buf);
}

GWBUF* gwbuf_clone(GWBUF* buf)
{
    validate_buffer(buf);

    GWBUF* rval = gwbuf_clone_one(buf);

    if (rval)
    {
        GWBUF* clonebuf = rval;

        while (clonebuf && buf->next)
        {
            buf = buf->next;
            clonebuf->next = gwbuf_clone_one(buf);
            clonebuf = clonebuf->next;
        }

        if (!clonebuf && buf->next)
        {
            // A gwbuf_clone failed, we need to free everything cloned sofar.
            gwbuf_free(rval);
            rval = NULL;
        }
        else
        {
            rval->tail = clonebuf;
        }

        invalidate_tail_pointers(rval);
    }

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

        invalidate_tail_pointers(*buf);
        invalidate_tail_pointers(head);
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
    mxb_assert(!head || validate_buffer(head));
    mxb_assert(validate_buffer(tail));

    if (!head)
    {
        return tail;
    }

    head->tail->next = tail;
    head->tail = tail->tail;

    invalidate_tail_pointers(head);

    return head;
}

GWBUF* gwbuf_consume(GWBUF* head, unsigned int length)
{
    validate_buffer(head);
    mxb_assert(length > 0);

    while (head && length > 0)
    {
        ensure_owned(head);
        unsigned int buflen = gwbuf_link_length(head);

        GWBUF_CONSUME(head, length);
        length = buflen < length ? length - buflen : 0;

        if (GWBUF_EMPTY(head))
        {
            if (head->next)
            {
                head->next->tail = head->tail;
            }
            GWBUF* tmp = head;
            head = head->next;
            gwbuf_free_one(tmp);
        }
    }

    invalidate_tail_pointers(head);

    mxb_assert(head == NULL || (head->end > head->start));
    return head;
}

unsigned int gwbuf_length(const GWBUF* head)
{
    validate_buffer(head);

    int rval = 0;

    while (head)
    {
        ensure_owned(head);
        rval += gwbuf_link_length(head);
        head = head->next;
    }

    return rval;
}

int gwbuf_count(const GWBUF* head)
{
    validate_buffer(head);

    int result = 0;

    while (head)
    {
        ensure_owned(head);
        result++;
        head = head->next;
    }

    return result;
}

GWBUF* gwbuf_rtrim(GWBUF* head, unsigned int n_bytes)
{
    validate_buffer(head);

    GWBUF* rval = head;
    GWBUF_RTRIM(head, n_bytes);

    if (GWBUF_EMPTY(head))
    {
        rval = head->next;
        gwbuf_free_one(head);
    }
    return rval;
}

void gwbuf_set_type(GWBUF* buf, uint32_t type)
{
    validate_buffer(buf);
    /** Set type consistenly to all buffers on the list */
    while (buf != NULL)
    {
        mxb_assert(buf->owner == RoutingWorker::get_current_id());
        buf->gwbuf_type |= type;
        buf = buf->next;
    }
}

void gwbuf_add_buffer_object(GWBUF* buf,
                             bufobj_id_t id,
                             void* data,
                             void (* donefun_fp)(void*))
{
    validate_buffer(buf);

    buffer_object_t* newb = (buffer_object_t*)MXS_MALLOC(sizeof(buffer_object_t));
    MXS_ABORT_IF_NULL(newb);

    newb->bo_id = id;
    newb->bo_data = data;
    newb->bo_donefun_fp = donefun_fp;
    newb->bo_next = NULL;

    buffer_object_t** p_b = &buf->sbuf->bufobj;
    /** Search the end of the list and add there */
    while (*p_b != NULL)
    {
        p_b = &(*p_b)->bo_next;
    }
    *p_b = newb;
    /** Set flag */
    buf->sbuf->info |= GWBUF_INFO_PARSED;
}

void* gwbuf_get_buffer_object_data(GWBUF* buf, bufobj_id_t id)
{
    validate_buffer(buf);

    buffer_object_t* bo = buf->sbuf->bufobj;

    while (bo != NULL && bo->bo_id != id)
    {
        bo = bo->bo_next;
    }

    return bo ? bo->bo_data : NULL;
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

///**
// * @return pointer to next buffer object or NULL
// */
// static buffer_object_t* gwbuf_remove_buffer_object(GWBUF* buf, buffer_object_t* bufobj)
// {
//    ensure_owned(buf);
//    buffer_object_t* next = bufobj->bo_next;
//    /** Call corresponding clean-up function to clean buffer object's data */
//    bufobj->bo_donefun_fp(bufobj->bo_data);
//    MXS_FREE(bufobj);
//    return next;
// }

GWBUF* gwbuf_make_contiguous(GWBUF* orig)
{
    validate_buffer(orig);

    if (orig->next == NULL)
    {
        // Already contiguous
        return orig;
    }

    GWBUF* newbuf = gwbuf_alloc(gwbuf_length(orig));
    MXS_ABORT_IF_NULL(newbuf);

    newbuf->gwbuf_type = orig->gwbuf_type;
    newbuf->hints = orig->hints;
    newbuf->id = orig->id;
    uint8_t* ptr = GWBUF_DATA(newbuf);

    while (orig)
    {
        int len = gwbuf_link_length(orig);
        memcpy(ptr, GWBUF_DATA(orig), len);
        ptr += len;
        orig = gwbuf_consume(orig, len);
    }

    return newbuf;
}

size_t gwbuf_copy_data(const GWBUF* buffer, size_t offset, size_t bytes, uint8_t* dest)
{
    uint32_t buflen;

    /** Skip unrelated buffers */
    while (buffer && offset && offset >= (buflen = gwbuf_link_length(buffer)))
    {
        mxb_assert(buffer->owner == RoutingWorker::get_current_id());
        offset -= buflen;
        buffer = buffer->next;
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
            do
            {
                memcpy(dest, ptr, bytes_left);
                bytes -= bytes_left;
                dest += bytes_left;
                bytes_read += bytes_left;
                buffer = buffer->next;

                if (buffer)
                {
                    bytes_left = MXS_MIN(gwbuf_link_length(buffer), bytes);
                    ptr = (uint8_t*) GWBUF_DATA(buffer);
                }
            }
            while (bytes > 0 && buffer);
        }
    }

    return bytes_read;
}

uint8_t* gwbuf_byte_pointer(GWBUF* buffer, size_t offset)
{
    validate_buffer(buffer);
    uint8_t* rval = NULL;
    // Ignore NULL buffer and walk past empty or too short buffers.
    while (buffer && (gwbuf_link_length(buffer) <= offset))
    {
        mxb_assert(buffer->owner == RoutingWorker::get_current_id());
        offset -= gwbuf_link_length(buffer);
        buffer = buffer->next;
    }

    if (buffer != NULL)
    {
        mxb_assert(buffer->owner == RoutingWorker::get_current_id());
        rval = (GWBUF_DATA(buffer) + offset);
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
    validate_buffer(buffer);
    mxb_assert(buffer->owner == RoutingWorker::get_current_id());
    std::stringstream ss;

    ss << "Buffer " << buffer << ":\n";

    for (GWBUF* b = buffer; b; b = b->next)
    {
        ss << dump_one_buffer(b);
    }

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
