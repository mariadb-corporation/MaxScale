/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/buffer.hh>

#include <errno.h>
#include <stdlib.h>
#include <sstream>

#include <maxbase/assert.h>
#include <maxscale/alloc.h>
#include <maxscale/hint.h>
#include <maxscale/utils.h>
#include <maxscale/routingworker.hh>

using mxs::RoutingWorker;

static void             gwbuf_free_one(GWBUF* buf);
static buffer_object_t* gwbuf_remove_buffer_object(GWBUF* buf,
                                                   buffer_object_t* bufobj);

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
    size_t sbuf_size = sizeof(SHARED_BUF) + (size ? size - 1 : 0);
    GWBUF* rval = (GWBUF*)MXS_MALLOC(sizeof(GWBUF));
    SHARED_BUF* sbuf = (SHARED_BUF*)MXS_MALLOC(sbuf_size);

    if (rval == NULL || sbuf == NULL)
    {
        MXS_FREE(rval);
        MXS_FREE(sbuf);
        return NULL;
    }

    sbuf->refcount = 1;
    sbuf->info = GWBUF_INFO_NONE;
    sbuf->bufobj = NULL;

#ifdef SS_DEBUG
    rval->owner = RoutingWorker::get_current_id();
#endif
    rval->start = &sbuf->data;
    rval->end = (void*)((char*)rval->start + size);
    rval->sbuf = sbuf;
    rval->next = NULL;
    rval->tail = rval;
    rval->hint = NULL;
    rval->properties = NULL;
    rval->gwbuf_type = GWBUF_TYPE_UNDEFINED;
    rval->server = NULL;

    return rval;
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
    while (buf)
    {
        mxb_assert(buf->owner == RoutingWorker::get_current_id());
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
    --buf->sbuf->refcount;

    if (buf->sbuf->refcount == 0)
    {
        buffer_object_t* bo = buf->sbuf->bufobj;

        while (bo != NULL)
        {
            bo = gwbuf_remove_buffer_object(buf, bo);
        }

        MXS_FREE(buf->sbuf);
    }

    while (buf->properties)
    {
        BUF_PROPERTY* prop = buf->properties;
        buf->properties = prop->next;
        MXS_FREE(prop->name);
        MXS_FREE(prop->value);
        MXS_FREE(prop);
    }
    /** Release the hint */
    while (buf->hint)
    {
        HINT* h = buf->hint;
        buf->hint = buf->hint->next;
        hint_free(h);
    }

    MXS_FREE(buf);
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
    GWBUF* rval = (GWBUF*)MXS_CALLOC(1, sizeof(GWBUF));

    if (rval == NULL)
    {
        return NULL;
    }

    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    ++buf->sbuf->refcount;
#ifdef SS_DEBUG
    rval->owner = RoutingWorker::get_current_id();
#endif
    rval->server = buf->server;
    rval->sbuf = buf->sbuf;
    rval->start = buf->start;
    rval->end = buf->end;
    rval->gwbuf_type = buf->gwbuf_type;
    rval->tail = rval;
    rval->next = NULL;

    return rval;
}

GWBUF* gwbuf_clone(GWBUF* buf)
{
    mxb_assert(buf);

    if (!buf)
    {
        return NULL;
    }

    mxb_assert(buf->owner == RoutingWorker::get_current_id());
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
    }

    return rval;
}

GWBUF* gwbuf_deep_clone(const GWBUF* buf)
{
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    GWBUF* rval = NULL;

    if (buf)
    {
        size_t buflen = gwbuf_length(buf);
        rval = gwbuf_alloc(buflen);

        if (rval && gwbuf_copy_data(buf, 0, buflen, GWBUF_DATA(rval)) == buflen)
        {
            rval->gwbuf_type = buf->gwbuf_type;
        }
        else
        {
            gwbuf_free(rval);
            rval = NULL;
        }
    }

    return rval;
}

static GWBUF* gwbuf_clone_portion(GWBUF* buf,
                                  size_t start_offset,
                                  size_t length)
{
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    mxb_assert(start_offset + length <= GWBUF_LENGTH(buf));

    GWBUF* clonebuf = (GWBUF*)MXS_MALLOC(sizeof(GWBUF));

    if (clonebuf == NULL)
    {
        return NULL;
    }

    ++buf->sbuf->refcount;
#ifdef SS_DEBUG
    clonebuf->owner = RoutingWorker::get_current_id();
#endif
    clonebuf->server = buf->server;
    clonebuf->sbuf = buf->sbuf;
    clonebuf->gwbuf_type = buf->gwbuf_type;     /*< clone info bits too */
    clonebuf->start = (void*)((char*)buf->start + start_offset);
    clonebuf->end = (void*)((char*)clonebuf->start + length);
    clonebuf->gwbuf_type = buf->gwbuf_type;     /*< clone the type for now */
    clonebuf->properties = NULL;
    clonebuf->hint = NULL;
    clonebuf->next = NULL;
    clonebuf->tail = clonebuf;

    return clonebuf;
}

GWBUF* gwbuf_split(GWBUF** buf, size_t length)
{
    GWBUF* head = NULL;

    if (length > 0 && buf && *buf)
    {
        GWBUF* buffer = *buf;
        GWBUF* orig_tail = buffer->tail;
        head = buffer;
        mxb_assert(buffer->owner == RoutingWorker::get_current_id());

        /** Handle complete buffers */
        while (buffer && length && length >= GWBUF_LENGTH(buffer))
        {
            length -= GWBUF_LENGTH(buffer);
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
                mxb_assert(GWBUF_LENGTH(buffer) > length);
                GWBUF* partial = gwbuf_clone_portion(buffer, 0, length);

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
    while (*buf && (GWBUF_LENGTH(*buf) <= *offset))
    {
        mxb_assert((*buf)->owner == RoutingWorker::get_current_id());
        *offset -= GWBUF_LENGTH(*buf);
        *buf = (*buf)->next;
    }

    mxb_assert(!*buf || (GWBUF_LENGTH(*buf) > *offset));

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
    int rv;

    if ((lhs == NULL) && (rhs == NULL))
    {
        rv = 0;
    }
    else if (lhs == NULL)
    {
        mxb_assert(rhs);
        rv = -1;
    }
    else if (rhs == NULL)
    {
        mxb_assert(lhs);
        rv = 1;
    }
    else
    {
        mxb_assert(lhs->owner == RoutingWorker::get_current_id()
                   && rhs->owner == RoutingWorker::get_current_id());
        mxb_assert(lhs && rhs);

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
    }

    return rv;
}

GWBUF* gwbuf_append(GWBUF* head, GWBUF* tail)
{
    mxb_assert(!head || head->owner == RoutingWorker::get_current_id());
    mxb_assert(!tail || tail->owner == RoutingWorker::get_current_id());

    if (!head)
    {
        return tail;
    }
    else if (!tail)
    {
        return head;
    }

    head->tail->next = tail;
    head->tail = tail->tail;

    return head;
}

GWBUF* gwbuf_consume(GWBUF* head, unsigned int length)
{
    while (head && length > 0)
    {
        mxb_assert(head->owner == RoutingWorker::get_current_id());
        unsigned int buflen = GWBUF_LENGTH(head);

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

    mxb_assert(head == NULL || (head->end >= head->start));
    return head;
}

unsigned int gwbuf_length(const GWBUF* head)
{
    int rval = 0;

    while (head)
    {
        mxb_assert(head->owner == RoutingWorker::get_current_id());
        rval += GWBUF_LENGTH(head);
        head = head->next;
    }

    return rval;
}

int gwbuf_count(const GWBUF* head)
{
    int result = 0;

    while (head)
    {
        mxb_assert(head->owner == RoutingWorker::get_current_id());
        result++;
        head = head->next;
    }

    return result;
}

GWBUF* gwbuf_rtrim(GWBUF* head, unsigned int n_bytes)
{
    mxb_assert(head->owner == RoutingWorker::get_current_id());
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
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
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
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    buffer_object_t* bo = buf->sbuf->bufobj;

    while (bo != NULL && bo->bo_id != id)
    {
        bo = bo->bo_next;
    }

    return bo ? bo->bo_data : NULL;
}

/**
 * @return pointer to next buffer object or NULL
 */
static buffer_object_t* gwbuf_remove_buffer_object(GWBUF* buf, buffer_object_t* bufobj)
{
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    buffer_object_t* next = bufobj->bo_next;
    /** Call corresponding clean-up function to clean buffer object's data */
    bufobj->bo_donefun_fp(bufobj->bo_data);
    MXS_FREE(bufobj);
    return next;
}

bool gwbuf_add_property(GWBUF* buf, const char* name, const char* value)
{
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    char* my_name = MXS_STRDUP(name);
    char* my_value = MXS_STRDUP(value);
    BUF_PROPERTY* prop = (BUF_PROPERTY*)MXS_MALLOC(sizeof(BUF_PROPERTY));

    if (!my_name || !my_value || !prop)
    {
        MXS_FREE(my_name);
        MXS_FREE(my_value);
        MXS_FREE(prop);
        return false;
    }

    prop->name = my_name;
    prop->value = my_value;
    prop->next = buf->properties;
    buf->properties = prop;

    return true;
}

char* gwbuf_get_property(GWBUF* buf, const char* name)
{
    mxb_assert(buf->owner == RoutingWorker::get_current_id());
    BUF_PROPERTY* prop = buf->properties;

    while (prop && strcmp(prop->name, name) != 0)
    {
        prop = prop->next;
    }

    return prop ? prop->value : NULL;
}

GWBUF* gwbuf_make_contiguous(GWBUF* orig)
{
    mxb_assert_message(orig != NULL, "gwbuf_make_contiguous: NULL buffer");
    mxb_assert(orig->owner == RoutingWorker::get_current_id());

    if (orig->next == NULL)
    {
        // Already contiguous
        return orig;
    }

    GWBUF* newbuf = gwbuf_alloc(gwbuf_length(orig));
    MXS_ABORT_IF_NULL(newbuf);

    newbuf->gwbuf_type = orig->gwbuf_type;
    newbuf->hint = hint_dup(orig->hint);
    uint8_t* ptr = GWBUF_DATA(newbuf);

    while (orig)
    {
        int len = GWBUF_LENGTH(orig);
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
    while (buffer && offset && offset >= (buflen = GWBUF_LENGTH(buffer)))
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
        uint32_t bytes_left = GWBUF_LENGTH(buffer) - offset;

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
                    bytes_left = MXS_MIN(GWBUF_LENGTH(buffer), bytes);
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
    uint8_t* rval = NULL;
    // Ignore NULL buffer and walk past empty or too short buffers.
    while (buffer && (GWBUF_LENGTH(buffer) <= offset))
    {
        mxb_assert(buffer->owner == RoutingWorker::get_current_id());
        offset -= GWBUF_LENGTH(buffer);
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
    mxb_assert(buffer->owner == RoutingWorker::get_current_id());
    std::string rval;
    int len = GWBUF_LENGTH(buffer);
    uint8_t* data = GWBUF_DATA(buffer);

    while (len > 0)
    {
        // Process the buffer in 40 byte chunks
        int n = MXS_MIN(40, len);
        char output[n * 2 + 1];
        gw_bin2hex(output, data, n);
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
