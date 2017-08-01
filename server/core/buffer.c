/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/buffer.h>
#include <errno.h>
#include <stdlib.h>
#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/debug.h>
#include <maxscale/spinlock.h>
#include <maxscale/hint.h>
#include <maxscale/log_manager.h>

#if defined(BUFFER_TRACE)
#include <maxscale/hashtable.h>
#include <execinfo.h>

static HASHTABLE *buffer_hashtable = NULL;
#endif

static void gwbuf_free_one(GWBUF *buf);
static buffer_object_t* gwbuf_remove_buffer_object(GWBUF*           buf,
                                                   buffer_object_t* bufobj);

#if defined(BUFFER_TRACE)
static void gwbuf_add_to_hashtable(GWBUF *buf);
static int bhashfn (void *key);
static int bcmpfn (void *key1, void *key2);
static void gwbuf_remove_from_hashtable(GWBUF *buf);
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
GWBUF *
gwbuf_alloc(unsigned int size)
{
    GWBUF      *rval;
    SHARED_BUF *sbuf;

    /* Allocate the buffer header */
    if ((rval = (GWBUF *)MXS_MALLOC(sizeof(GWBUF))) == NULL)
    {
        goto retblock;
    }

    /* Allocate the shared data buffer */
    if ((sbuf = (SHARED_BUF *)MXS_MALLOC(sizeof(SHARED_BUF))) == NULL)
    {
        MXS_FREE(rval);
        rval = NULL;
        goto retblock;
    }

    /* Allocate the space for the actual data */
    if ((sbuf->data = (unsigned char *)MXS_MALLOC(size)) == NULL)
    {
        MXS_FREE(rval);
        MXS_FREE(sbuf);
        rval = NULL;
        goto retblock;
    }
    sbuf->refcount = 1;
    sbuf->info = GWBUF_INFO_NONE;
    sbuf->bufobj = NULL;

    spinlock_init(&rval->gwbuf_lock);
    rval->start = sbuf->data;
    rval->end = (void *)((char *)rval->start + size);
    rval->sbuf = sbuf;
    rval->next = NULL;
    rval->tail = rval;
    rval->hint = NULL;
    rval->properties = NULL;
    rval->gwbuf_type = GWBUF_TYPE_UNDEFINED;
    CHK_GWBUF(rval);
retblock:
    if (rval == NULL)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Memory allocation failed due to %s.",
                  strerror_r(errno, errbuf, sizeof(errbuf)));
    }
#if defined(BUFFER_TRACE)
    else
    {
        gwbuf_add_to_hashtable(rval);
    }
#endif
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
GWBUF *
gwbuf_alloc_and_load(unsigned int size, const void *data)
{
    GWBUF      *rval;
    if ((rval = gwbuf_alloc(size)) != NULL)
    {
        memcpy(GWBUF_DATA(rval), data, size);
    }
    return rval;
}

#if defined(BUFFER_TRACE)
/**
 * Store a trace of buffer creation
 *
 * @param buf The buffer to record
 */
static void
gwbuf_add_to_hashtable(GWBUF *buf)
{
    void *array[16];
    size_t size, i, total;
    char **strings;
    char *tracetext;

    size = backtrace(array, 16);
    strings = backtrace_symbols(array, size);
    total = (2 * size) + 1;
    for (i = 0; i < size; i++)
    {
        total += strlen(strings[i]);
    }
    tracetext = (char *)MXS_MALLOC(total);
    if (tracetext)
    {
        char *ptr = tracetext;
        for (i = 0; i < size; i++)
        {
            sprintf(ptr, "\t%s\n", strings[i]);
            ptr += (strlen(strings[i]) + 2);
        }
        free (strings);

        if (NULL == buffer_hashtable)
        {
            buffer_hashtable = hashtable_alloc(10000, bhashfn, bcmpfn);
            hashtable_memory_fns(buffer_hashtable, NULL, NULL, NULL, hashtable_item_free);
        }
        hashtable_add(buffer_hashtable, buf, (void *)tracetext);
    }
}

/**
 * Hash a buffer (address) to an integer
 *
 * @param key The pointer to the buffer
 */
static int
bhashfn(void *key)
{
    return (int)((uintptr_t) key % INT_MAX);
}

/**
 * Compare two buffer keys (pointers)
 *
 * @param key1 The pointer to the first buffer
 * @param key2 The pointer to the second buffer
 */
static int
bcmpfn(void *key1, void *key2)
{
    return key1 == key2 ? 0 : 1;
}

/**
 * Remove a buffer from the store of buffer traces
 *
 * @param buf The buffer to be removed
 */
static void
gwbuf_remove_from_hashtable(GWBUF *buf)
{
    hashtable_delete(buffer_hashtable, buf);
}

/**
 * Print all buffer traces via a given print DCB
 *
 * @param pdcb  Print DCB for output
 */
void
dprintAllBuffers(void *pdcb)
{
    void *buf;
    char *backtrace;
    HASHITERATOR *buffers = hashtable_iterator(buffer_hashtable);
    while (NULL != (buf = hashtable_next(buffers)))
    {
        dcb_printf((DCB *)pdcb, "Buffer: %p\n", (void *)buf);
        backtrace = hashtable_fetch(buffer_hashtable, buf);
        dcb_printf((DCB *)pdcb, "%s", backtrace);
    }
    hashtable_iterator_free(buffers);
}
#endif

/**
 * Free a list of gateway buffers
 *
 * @param buf The head of the list of buffers to free
 */
void
gwbuf_free(GWBUF *buf)
{
    GWBUF *nextbuf;
    BUF_PROPERTY    *prop;
    buffer_object_t *bo;

    while (buf)
    {
        CHK_GWBUF(buf);
        nextbuf = buf->next;
        gwbuf_free_one(buf);
        buf = nextbuf;
    }
}

/**
 * Free a single gateway buffer
 *
 * @param buf The buffer to free
 */
static void
gwbuf_free_one(GWBUF *buf)
{
    BUF_PROPERTY    *prop;
    buffer_object_t *bo;

    if (atomic_add(&buf->sbuf->refcount, -1) == 1)
    {
        MXS_FREE(buf->sbuf->data);
        MXS_FREE(buf->sbuf);
        bo = buf->sbuf->bufobj;

        while (bo != NULL)
        {
            bo = gwbuf_remove_buffer_object(buf, bo);
        }

    }
    while (buf->properties)
    {
        prop = buf->properties;
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
#if defined(BUFFER_TRACE)
    gwbuf_remove_from_hashtable(buf);
#endif
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
static GWBUF *
gwbuf_clone_one(GWBUF *buf)
{
    GWBUF *rval;

    if ((rval = (GWBUF *)MXS_CALLOC(1, sizeof(GWBUF))) == NULL)
    {
        return NULL;
    }

    atomic_add(&buf->sbuf->refcount, 1);
    rval->sbuf = buf->sbuf;
    rval->start = buf->start;
    rval->end = buf->end;
    rval->gwbuf_type = buf->gwbuf_type;
    rval->tail = rval;
    rval->next = NULL;
    CHK_GWBUF(rval);
#if defined(BUFFER_TRACE)
    gwbuf_add_to_hashtable(rval);
#endif
    return rval;
}

GWBUF* gwbuf_clone(GWBUF* buf)
{
    if (buf == NULL)
    {
        return NULL;
    }

    GWBUF *rval = gwbuf_clone_one(buf);

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


static GWBUF *gwbuf_clone_portion(GWBUF *buf,
                                  size_t start_offset,
                                  size_t length)
{
    GWBUF* clonebuf;

    CHK_GWBUF(buf);
    ss_dassert(start_offset + length <= GWBUF_LENGTH(buf));

    if ((clonebuf = (GWBUF *)MXS_MALLOC(sizeof(GWBUF))) == NULL)
    {
        return NULL;
    }
    atomic_add(&buf->sbuf->refcount, 1);
    clonebuf->sbuf = buf->sbuf;
    clonebuf->gwbuf_type = buf->gwbuf_type; /*< clone info bits too */
    clonebuf->start = (void *)((char*)buf->start + start_offset);
    clonebuf->end = (void *)((char *)clonebuf->start + length);
    clonebuf->gwbuf_type = buf->gwbuf_type; /*< clone the type for now */
    clonebuf->properties = NULL;
    clonebuf->hint = NULL;
    clonebuf->next = NULL;
    clonebuf->tail = clonebuf;
    CHK_GWBUF(clonebuf);
#if defined(BUFFER_TRACE)
    gwbuf_add_to_hashtable(clonebuf);
#endif
    return clonebuf;
}

GWBUF* gwbuf_split(GWBUF **buf, size_t length)
{
    GWBUF* head = NULL;

    if (length > 0 && buf && *buf)
    {
        GWBUF* buffer = *buf;
        GWBUF* orig_tail = buffer->tail;
        head = buffer;

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
                ss_dassert(GWBUF_LENGTH(buffer) > length);
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
        *offset -= GWBUF_LENGTH(*buf);
        *buf = (*buf)->next;
    }

    ss_dassert(!*buf || (GWBUF_LENGTH(*buf) > *offset));

    if (*buf)
    {
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
        ss_dassert(rhs);
        rv = -1;
    }
    else if (rhs == NULL)
    {
        ss_dassert(lhs);
        rv = 1;
    }
    else
    {
        ss_dassert(lhs && rhs);

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
            ss_dassert(llen == rlen);

            rv = 0;
            size_t i = 0;
            size_t loffset = 0;
            size_t roffset = 0;

            while ((rv == 0) && (i < llen))
            {
                uint8_t lc;
                uint8_t rc;

                ss_debug(bool rv1 = ) gwbuf_get_byte(&lhs, &loffset, &lc);
                ss_debug(bool rv2 = ) gwbuf_get_byte(&rhs, &roffset, &rc);

                ss_dassert(rv1 && rv2);

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

GWBUF *
gwbuf_append(GWBUF *head, GWBUF *tail)
{
    if (!head)
    {
        return tail;
    }
    if (!tail)
    {
        return head;
    }
    CHK_GWBUF(head);
    head->tail->next = tail;
    head->tail = tail->tail;

    return head;
}

GWBUF *
gwbuf_consume(GWBUF *head, unsigned int length)
{
    while (head && length > 0)
    {
        CHK_GWBUF(head);
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

    ss_dassert(head == NULL || (head->end >= head->start));
    return head;
}

unsigned int
gwbuf_length(const GWBUF *head)
{
    int rval = 0;

    if (head)
    {
        CHK_GWBUF(head);
    }
    while (head)
    {
        rval += GWBUF_LENGTH(head);
        head = head->next;
    }
    return rval;
}

int
gwbuf_count(const GWBUF *head)
{
    int result = 0;
    while (head)
    {
        result++;
        head = head->next;
    }
    return result;
}

GWBUF *
gwbuf_rtrim(GWBUF *head, unsigned int n_bytes)
{
    GWBUF *rval = head;
    CHK_GWBUF(head);
    GWBUF_RTRIM(head, n_bytes);
    CHK_GWBUF(head);

    if (GWBUF_EMPTY(head))
    {
        rval = head->next;
        gwbuf_free_one(head);
    }
    return rval;
}

void gwbuf_set_type(GWBUF* buf, gwbuf_type_t type)
{
    /** Set type consistenly to all buffers on the list */
    while (buf != NULL)
    {
        CHK_GWBUF(buf);
        buf->gwbuf_type |= type;
        buf = buf->next;
    }
}

void gwbuf_add_buffer_object(GWBUF* buf,
                             bufobj_id_t id,
                             void*  data,
                             void (*donefun_fp)(void *))
{
    buffer_object_t** p_b;
    buffer_object_t*  newb;

    CHK_GWBUF(buf);
    newb = (buffer_object_t *)MXS_MALLOC(sizeof(buffer_object_t));
    MXS_ABORT_IF_NULL(newb);

    newb->bo_id = id;
    newb->bo_data = data;
    newb->bo_donefun_fp = donefun_fp;
    newb->bo_next = NULL;
    /** Lock */
    spinlock_acquire(&buf->gwbuf_lock);
    p_b = &buf->sbuf->bufobj;
    /** Search the end of the list and add there */
    while (*p_b != NULL)
    {
        p_b = &(*p_b)->bo_next;
    }
    *p_b = newb;
    /** Set flag */
    buf->sbuf->info |= GWBUF_INFO_PARSED;
    /** Unlock */
    spinlock_release(&buf->gwbuf_lock);
}

void* gwbuf_get_buffer_object_data(GWBUF* buf, bufobj_id_t id)
{
    buffer_object_t* bo;

    CHK_GWBUF(buf);
    /** Lock */
    spinlock_acquire(&buf->gwbuf_lock);
    bo = buf->sbuf->bufobj;

    while (bo != NULL && bo->bo_id != id)
    {
        bo = bo->bo_next;
    }
    /** Unlock */
    spinlock_release(&buf->gwbuf_lock);
    if (bo)
    {
        return bo->bo_data;
    }
    return NULL;
}

/**
 * @return pointer to next buffer object or NULL
 */
static buffer_object_t* gwbuf_remove_buffer_object(GWBUF* buf, buffer_object_t* bufobj)
{
    buffer_object_t* next;

    next = bufobj->bo_next;
    /** Call corresponding clean-up function to clean buffer object's data */
    bufobj->bo_donefun_fp(bufobj->bo_data);
    MXS_FREE(bufobj);
    return next;
}

bool
gwbuf_add_property(GWBUF *buf, char *name, char *value)
{
    name = MXS_STRDUP(name);
    value = MXS_STRDUP(value);

    BUF_PROPERTY *prop = (BUF_PROPERTY *)MXS_MALLOC(sizeof(BUF_PROPERTY));

    if (!name || !value || !prop)
    {
        MXS_FREE(name);
        MXS_FREE(value);
        MXS_FREE(prop);
        return false;
    }

    prop->name = name;
    prop->value = value;
    spinlock_acquire(&buf->gwbuf_lock);
    prop->next = buf->properties;
    buf->properties = prop;
    spinlock_release(&buf->gwbuf_lock);
    return true;
}

char *
gwbuf_get_property(GWBUF *buf, char *name)
{
    BUF_PROPERTY *prop;

    spinlock_acquire(&buf->gwbuf_lock);
    prop = buf->properties;
    while (prop && strcmp(prop->name, name) != 0)
    {
        prop = prop->next;
    }
    spinlock_release(&buf->gwbuf_lock);
    if (prop)
    {
        return prop->value;
    }
    return NULL;
}

GWBUF *
gwbuf_make_contiguous(GWBUF *orig)
{
    GWBUF   *newbuf;
    uint8_t *ptr;
    int     len;

    if (orig == NULL)
    {
        return NULL;
    }
    if (orig->next == NULL)
    {
        return orig;
    }

    if ((newbuf = gwbuf_alloc(gwbuf_length(orig))) != NULL)
    {
        newbuf->gwbuf_type = orig->gwbuf_type;
        newbuf->hint = hint_dup(orig->hint);
        ptr = GWBUF_DATA(newbuf);

        while (orig)
        {
            len = GWBUF_LENGTH(orig);
            memcpy(ptr, GWBUF_DATA(orig), len);
            ptr += len;
            orig = gwbuf_consume(orig, len);
        }
    }
    return newbuf;
}

void
gwbuf_add_hint(GWBUF *buf, HINT *hint)
{
    HINT *ptr;

    spinlock_acquire(&buf->gwbuf_lock);
    if (buf->hint)
    {
        ptr = buf->hint;
        while (ptr->next)
        {
            ptr = ptr->next;
        }
        ptr->next = hint;
    }
    else
    {
        buf->hint = hint;
    }
    spinlock_release(&buf->gwbuf_lock);
}

size_t gwbuf_copy_data(const GWBUF *buffer, size_t offset, size_t bytes, uint8_t* dest)
{
    uint32_t buflen;

    /** Skip unrelated buffers */
    while (buffer && offset && offset >= (buflen = GWBUF_LENGTH(buffer)))
    {
        offset -= buflen;
        buffer = buffer->next;
    }

    size_t bytes_read = 0;

    if (buffer)
    {
        uint8_t *ptr = (uint8_t*) GWBUF_DATA(buffer) + offset;
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
