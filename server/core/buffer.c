/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file buffer.h  - The MaxScale buffer management functions
 *
 * The buffer management is based on the principle of a linked list
 * of variable size buffer, the intention beign to allow longer
 * content to be buffered in a list and minimise any need to copy
 * data between buffers.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 10/06/13     Mark Riddoch            Initial implementation
 * 11/07/13     Mark Riddoch            Add reference count mechanism
 * 16/07/2013   Massimiliano Pinto      Added command type to gwbuf struct
 * 24/06/2014   Mark Riddoch            Addition of gwbuf_trim
 * 15/07/2014   Mark Riddoch            Addition of properties
 * 28/08/2014   Mark Riddoch            Adition of tail pointer to speed
 *                                      the gwbuf_append process
 * 09/11/2015   Martin Brampton         Add buffer tracing (conditional compilation),
 *                                      accessed by "show buffers" maxadmin command
 *
 * @endverbatim
 */
#include <stdlib.h>
#include <buffer.h>
#include <atomic.h>
#include <skygw_debug.h>
#include <spinlock.h>
#include <hint.h>
#include <log_manager.h>
#include <errno.h>

#if defined(BUFFER_TRACE)
#include <hashtable.h>
#include <execinfo.h>

static HASHTABLE *buffer_hashtable = NULL;
#endif

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
 * to a more effecient mechanism.
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
    if ((rval = (GWBUF *)malloc(sizeof(GWBUF))) == NULL)
    {
        goto retblock;
    }

    /* Allocate the shared data buffer */
    if ((sbuf = (SHARED_BUF *)malloc(sizeof(SHARED_BUF))) == NULL)
    {
        free(rval);
        rval = NULL;
        goto retblock;
    }

    /* Allocate the space for the actual data */
    if ((sbuf->data = (unsigned char *)malloc(size)) == NULL)
    {
        ss_dassert(sbuf->data != NULL);
        free(rval);
        free(sbuf);
        rval = NULL;
        goto retblock;
    }
    spinlock_init(&rval->gwbuf_lock);
    rval->start = sbuf->data;
    rval->end = (void *)((char *)rval->start+size);
    sbuf->refcount = 1;
    rval->sbuf = sbuf;
    rval->next = NULL;
    rval->tail = rval;
    rval->hint = NULL;
    rval->properties = NULL;
    rval->gwbuf_type = GWBUF_TYPE_UNDEFINED;
    rval->gwbuf_info = GWBUF_INFO_NONE;
    rval->gwbuf_bufobj = NULL;
    CHK_GWBUF(rval);
retblock:
    if (rval == NULL)
    {
        char errbuf[STRERROR_BUFLEN];
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
    tracetext = (char *)malloc(total);
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
            hashtable_memory_fns(buffer_hashtable, NULL, NULL, NULL, (HASHMEMORYFN)free);
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
 * Free a gateway buffer
 *
 * @param buf The buffer to free
 */
void
gwbuf_free(GWBUF *buf)
{
    BUF_PROPERTY    *prop;
    buffer_object_t *bo;

    CHK_GWBUF(buf);
    if (atomic_add(&buf->sbuf->refcount, -1) == 1)
    {
        free(buf->sbuf->data);
        free(buf->sbuf);
        bo = buf->gwbuf_bufobj;

        while (bo != NULL)
        {
            bo = gwbuf_remove_buffer_object(buf, bo);
        }

    }
    while (buf->properties)
    {
        prop = buf->properties;
        buf->properties = prop->next;
        free(prop->name);
        free(prop->value);
        free(prop);
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
    free(buf);
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
GWBUF *
gwbuf_clone(GWBUF *buf)
{
    GWBUF *rval;

    if ((rval = (GWBUF *)calloc(1,sizeof(GWBUF))) == NULL)
    {
        ss_dassert(rval != NULL);
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Memory allocation failed due to %s.",
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return NULL;
    }

    atomic_add(&buf->sbuf->refcount, 1);
    rval->sbuf = buf->sbuf;
    rval->start = buf->start;
    rval->end = buf->end;
    rval->gwbuf_type = buf->gwbuf_type;
    rval->gwbuf_info = buf->gwbuf_info;
    rval->gwbuf_bufobj = buf->gwbuf_bufobj;
    rval->tail = rval;
    rval->next = NULL;
    CHK_GWBUF(rval);
#if defined(BUFFER_TRACE)
    gwbuf_add_to_hashtable(rval);
#endif
    return rval;
}

/**
 * Clone whole GWBUF list instead of single buffer.
 *
 * @param buf   head of the list to be cloned till the tail of it
 *
 * @return head of the cloned list or NULL if the list was empty.
 */
GWBUF* gwbuf_clone_all(GWBUF* buf)
{
    GWBUF* rval;
    GWBUF* clonebuf;

    if (buf == NULL)
    {
        return NULL;
    }
    /** Store the head of the list to rval. */
    clonebuf = gwbuf_clone(buf);
    rval = clonebuf;

    while (buf->next)
    {
        buf = buf->next;
        clonebuf->next = gwbuf_clone(buf);
        clonebuf = clonebuf->next;
    }
    return rval;
}


GWBUF *gwbuf_clone_portion(GWBUF *buf,
                           size_t start_offset,
                           size_t length)
{
    GWBUF* clonebuf;

    CHK_GWBUF(buf);
    ss_dassert(start_offset+length <= GWBUF_LENGTH(buf));

    if ((clonebuf = (GWBUF *)malloc(sizeof(GWBUF))) == NULL)
    {
        ss_dassert(clonebuf != NULL);
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Memory allocation failed due to %s.",
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return NULL;
    }
    atomic_add(&buf->sbuf->refcount, 1);
    clonebuf->sbuf = buf->sbuf;
    clonebuf->gwbuf_type = buf->gwbuf_type; /*< clone info bits too */
    clonebuf->start = (void *)((char*)buf->start+start_offset);
    clonebuf->end = (void *)((char *)clonebuf->start+length);
    clonebuf->gwbuf_type = buf->gwbuf_type; /*< clone the type for now */
    clonebuf->properties = NULL;
    clonebuf->hint = NULL;
    clonebuf->gwbuf_info = buf->gwbuf_info;
    clonebuf->gwbuf_bufobj = buf->gwbuf_bufobj;
    clonebuf->next = NULL;
    clonebuf->tail = clonebuf;
    CHK_GWBUF(clonebuf);
#if defined(BUFFER_TRACE)
    gwbuf_add_to_hashtable(clonebuf);
#endif
    return clonebuf;
}

/**
 * Returns pointer to GWBUF of a requested type.
 * As of 10.3.14 only MySQL to plain text conversion is supported.
 * Return NULL if conversion between types is not supported or due lacking
 * type information.
 */
GWBUF *gwbuf_clone_transform(GWBUF *head, gwbuf_type_t targettype)
{
    gwbuf_type_t src_type;
    GWBUF*       clonebuf;

    CHK_GWBUF(head);
    src_type = head->gwbuf_type;

    if (targettype == GWBUF_TYPE_UNDEFINED ||
        src_type == GWBUF_TYPE_UNDEFINED ||
        src_type == GWBUF_TYPE_PLAINSQL ||
        targettype == src_type)
    {
        clonebuf = NULL;
        goto return_clonebuf;
    }

    if (GWBUF_IS_TYPE_MYSQL(head))
    {
        if (GWBUF_TYPE_PLAINSQL == targettype)
        {
            /** Crete reference to string part of buffer */
            clonebuf = gwbuf_clone_portion(head,
                                           5,
                                           GWBUF_LENGTH(head) - 5);
            ss_dassert(clonebuf != NULL);
            /** Overwrite the type with new format */
            gwbuf_set_type(clonebuf, targettype);
        }
        else
        {
            clonebuf = NULL;
        }
    }
    else
    {
        clonebuf = NULL;
    }

return_clonebuf:
    return clonebuf;
}


/**
 * Append a buffer onto a linked list of buffer structures.
 *
 * This call should be made with the caller holding the lock for the linked
 * list.
 *
 * @param head  The current head of the linked list
 * @param tail  The new buffer to make the tail of the linked list
 * @return      The new head of the linked list
 */
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

/**
 * Consume data from a buffer in the linked list. The assumption is to consume
 * n bytes from the buffer chain.
 *
 * If after consuming the bytes from the first buffer that buffer becomes
 * empty it will be freed and the linked list updated.
 *
 * The return value is the new head of the linked list.
 *
 * This call should be made with the caller holding the lock for the linked
 * list.
 *
 * @param head          The head of the linked list
 * @param length        The amount of data to consume
 * @return The head of the linked list
 */
GWBUF *
gwbuf_consume(GWBUF *head, unsigned int length)
{
    GWBUF *rval = head;

    CHK_GWBUF(head);
    GWBUF_CONSUME(head, length);
    CHK_GWBUF(head);

    if (GWBUF_EMPTY(head))
    {
        rval = head->next;
        if (head->next)
        {
            head->next->tail = head->tail;
        }

        gwbuf_free(head);
    }

    ss_dassert(rval == NULL || (rval->end > rval->start));
    return rval;
}

/**
 * Return the number of bytes of data in the linked list.
 *
 * @param head  The current head of the linked list
 * @return The number of bytes of data in the linked list
 */
unsigned int
gwbuf_length(GWBUF *head)
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

/**
 * Trim bytes form the end of a GWBUF structure. If the
 * buffer has n_bytes or less then it will be freed and
 * NULL will be returned.
 *
 * This routine assumes the buffer is not part of a chain
 *
 * @param buf           The buffer to trim
 * @param n_bytes       The number of bytes to trim off
 * @return              The buffer chain or NULL if buffer has <= n_bytes
 */
GWBUF *
gwbuf_trim(GWBUF *buf, unsigned int n_bytes)
{
    ss_dassert(buf->next == NULL);

    if (GWBUF_LENGTH(buf) <= n_bytes)
    {
        gwbuf_consume(buf, GWBUF_LENGTH(buf));
        return NULL;
    }
    buf->end = (void *)((char *)buf->end - n_bytes);

    return buf;
}

/**
 * Trim bytes from the end of a GWBUF structure that may be the first
 * in a list. If the buffer has n_bytes or less then it will be freed and
 * the next buffer in the list will be returned, or if none, NULL.
 *
 * @param head          The buffer to trim
 * @param n_bytes       The number of bytes to trim off
 * @return              The buffer chain or NULL if buffer chain now empty
 */
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
        gwbuf_free(head);
    }
    return rval;
}

/**
 * Set given type to all buffers on the list.
 * *
 * @param buf           The shared buffer
 * @param type          Type to be added
 */
void gwbuf_set_type(
    GWBUF*       buf,
    gwbuf_type_t type)
{
    /** Set type consistenly to all buffers on the list */
    while (buf != NULL)
    {
        CHK_GWBUF(buf);
        buf->gwbuf_type |= type;
        buf=buf->next;
    }
}

/**
 * Add a buffer object to GWBUF buffer.
 *
 * @param buf           GWBUF where object is added
 * @param id            Type identifier for object
 * @param data          Object data
 * @param donefun_dp    Clean-up function to be executed before buffer is freed.
 */
void gwbuf_add_buffer_object(GWBUF* buf,
                             bufobj_id_t id,
                             void*  data,
                             void (*donefun_fp)(void *))
{
    buffer_object_t** p_b;
    buffer_object_t*  newb;

    CHK_GWBUF(buf);
    newb = (buffer_object_t *)malloc(sizeof(buffer_object_t));
    ss_dassert(newb != NULL);

    if (newb == NULL)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Memory allocation failed due to %s.",
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return;
    }
    newb->bo_id = id;
    newb->bo_data = data;
    newb->bo_donefun_fp = donefun_fp;
    newb->bo_next = NULL;
    /** Lock */
    spinlock_acquire(&buf->gwbuf_lock);
    p_b = &buf->gwbuf_bufobj;
    /** Search the end of the list and add there */
    while (*p_b != NULL)
    {
        p_b = &(*p_b)->bo_next;
    }
    *p_b = newb;
    /** Set flag */
    buf->gwbuf_info |= GWBUF_INFO_PARSED;
    /** Unlock */
    spinlock_release(&buf->gwbuf_lock);
}

/**
 * Search buffer object which matches with the id.
 *
 * @param buf   GWBUF to be searched
 * @param id    Identifier for the object
 *
 * @return Searched buffer object or NULL if not found
 */
void* gwbuf_get_buffer_object_data(GWBUF* buf, bufobj_id_t id)
{
    buffer_object_t* bo;

    CHK_GWBUF(buf);
    /** Lock */
    spinlock_acquire(&buf->gwbuf_lock);
    bo = buf->gwbuf_bufobj;

    while (bo != NULL && bo->bo_id != id)
    {
        bo = bo->bo_next;
    }
    /** Unlock */
    spinlock_release(&buf->gwbuf_lock);
    if(bo){
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
    free(bufobj);
    return next;
}

/**
 * Add a property to a buffer.
 *
 * @param buf   The buffer to add the property to
 * @param name  The property name
 * @param value The property value
 * @return      Non-zero on success
 */
int
gwbuf_add_property(GWBUF *buf, char *name, char *value)
{
    BUF_PROPERTY *prop;

    if ((prop = malloc(sizeof(BUF_PROPERTY))) == NULL)
    {
        ss_dassert(prop != NULL);
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Memory allocation failed due to %s.",
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return 0;
    }
    prop->name = strdup(name);
    prop->value = strdup(value);
    spinlock_acquire(&buf->gwbuf_lock);
    prop->next = buf->properties;
    buf->properties = prop;
    spinlock_release(&buf->gwbuf_lock);
    return 1;
}

/**
 * Return the value of a buffer property
 * @param buf   The buffer itself
 * @param name  The name of the property to return
 * @return The property value or NULL if the property was not found.
 */
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


/**
 * Convert a chain of GWBUF structures into a single GWBUF structure
 *
 * @param orig          The chain to convert
 * @return              The contiguous buffer
 */
GWBUF *
gwbuf_make_contiguous(GWBUF *orig)
{
    GWBUF *newbuf;
    char  *ptr;
    int   len;

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

/**
 * Add hint to a buffer.
 *
 * @param buf   The buffer to add the hint to
 * @param hint  The hint itself
 * @return      Non-zero on success
 */
int
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
    return 1;
}
