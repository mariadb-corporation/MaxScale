#pragma once
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

/**
 * @file buffer.h  Definitions relating the gateway buffer manipulation facilities.
 *
 * These are used to store all data coming in form or going out to the client and the
 * backend structures.
 *
 * The buffers are designed to be used in linked lists and such that they may be passed
 * from one side of the gateway to another without the need to copy data. It may be the case
 * that not all of the data in the buffer is valid, to this end a start and end pointer are
 * included that point to the first valid byte in the buffer and the first byte after the
 * last valid byte. This allows data to be consumed from either end of the buffer whilst
 * still allowing for the copy free semantics of the buffering system.
 */

#include <maxscale/cdefs.h>
#include <string.h>
#include <maxscale/debug.h>
#include <maxscale/hint.h>
#include <maxscale/spinlock.h>
#include <stdint.h>

MXS_BEGIN_DECLS

struct server;

/**
 * Buffer properties - used to store properties related to the buffer
 * contents. This may be added at any point during the processing of the
 * data, especially in the protocol stage of the processing.
 */
typedef struct buf_property
{
    char                    *name;
    char                    *value;
    struct buf_property     *next;
} BUF_PROPERTY;

typedef enum
{
    GWBUF_TYPE_UNDEFINED       = 0x00,
    GWBUF_TYPE_PLAINSQL        = 0x01,
    GWBUF_TYPE_MYSQL           = 0x02,
    GWBUF_TYPE_SINGLE_STMT     = 0x04,
    GWBUF_TYPE_SESCMD_RESPONSE = 0x08,
    GWBUF_TYPE_RESPONSE_END    = 0x10,
    GWBUF_TYPE_SESCMD          = 0x20,
    GWBUF_TYPE_HTTP            = 0x40
} gwbuf_type_t;

#define GWBUF_IS_TYPE_UNDEFINED(b)       (b->gwbuf_type == 0)
#define GWBUF_IS_TYPE_PLAINSQL(b)        (b->gwbuf_type & GWBUF_TYPE_PLAINSQL)
#define GWBUF_IS_TYPE_MYSQL(b)           (b->gwbuf_type & GWBUF_TYPE_MYSQL)
#define GWBUF_IS_TYPE_SINGLE_STMT(b)     (b->gwbuf_type & GWBUF_TYPE_SINGLE_STMT)
#define GWBUF_IS_TYPE_SESCMD_RESPONSE(b) (b->gwbuf_type & GWBUF_TYPE_SESCMD_RESPONSE)
#define GWBUF_IS_TYPE_RESPONSE_END(b)    (b->gwbuf_type & GWBUF_TYPE_RESPONSE_END)
#define GWBUF_IS_TYPE_SESCMD(b)          (b->gwbuf_type & GWBUF_TYPE_SESCMD)

typedef enum
{
    GWBUF_INFO_NONE         = 0x0,
    GWBUF_INFO_PARSED       = 0x1
} gwbuf_info_t;

#define GWBUF_IS_PARSED(b)      (b->sbuf->info & GWBUF_INFO_PARSED)

/**
 * A structure for cleaning up memory allocations of structures which are
 * referred to by GWBUF and deallocated in gwbuf_free but GWBUF doesn't
 * know what they are.
 * All functions on the list are executed before freeing memory of GWBUF struct.
 */
typedef enum
{
    GWBUF_PARSING_INFO
} bufobj_id_t;

typedef struct buffer_object_st buffer_object_t;

struct buffer_object_st
{
    bufobj_id_t      bo_id;
    void*            bo_data;
    void            (*bo_donefun_fp)(void *);
    buffer_object_t* bo_next;
};

/**
 * A structure to encapsulate the data in a form that the data itself can be
 * shared between multiple GWBUF's without the need to make multiple copies
 * but still maintain separate data pointers.
 */
typedef struct
{
    unsigned char   *data;     /*< Physical memory that was allocated */
    int              refcount; /*< Reference count on the buffer */
    buffer_object_t *bufobj;   /*< List of objects referred to by GWBUF */
    gwbuf_info_t     info;     /*< Info bits */
} SHARED_BUF;

/**
 * The buffer structure used by the descriptor control blocks.
 *
 * Linked lists of buffers are created as data is read from a descriptor
 * or written to a descriptor. The use of linked lists of buffers with
 * flexible data pointers is designed to minimise the need for data to
 * be copied within the gateway.
 */
typedef struct gwbuf
{
    SPINLOCK        gwbuf_lock;
    struct gwbuf    *next;  /*< Next buffer in a linked chain of buffers */
    struct gwbuf    *tail;  /*< Last buffer in a linked chain of buffers */
    void            *start; /*< Start of the valid data */
    void            *end;   /*< First byte after the valid data */
    SHARED_BUF      *sbuf;  /*< The shared buffer with the real data */
    gwbuf_type_t    gwbuf_type; /*< buffer's data type information */
    HINT            *hint;  /*< Hint data for this buffer */
    BUF_PROPERTY    *properties; /*< Buffer properties */
    struct server   *server; /*< The target server where the buffer is executed */
} GWBUF;

/*<
 * Macros to access the data in the buffers
 */
/*< First valid, unconsumed byte in the buffer */
#define GWBUF_DATA(b)           ((uint8_t*)(b)->start)

/*< Number of bytes in the individual buffer */
#define GWBUF_LENGTH(b)         ((char *)(b)->end - (char *)(b)->start)

/*< Return the byte at offset byte from the start of the unconsumed portion of the buffer */
#define GWBUF_DATA_CHAR(b, byte)    (GWBUF_LENGTH(b) < ((byte)+1) ? -1 : *(((char *)(b)->start)+4))

/*< Check that the data in a buffer has the SQL marker*/
#define GWBUF_IS_SQL(b)         (0x03 == GWBUF_DATA_CHAR(b,4))

/*< Check whether the buffer is contiguous*/
#define GWBUF_IS_CONTIGUOUS(b) (((b) == NULL) || ((b)->next == NULL))

/*< True if all bytes in the buffer have been consumed */
#define GWBUF_EMPTY(b)          ((char *)(b)->start >= (char *)(b)->end)

/*< Consume a number of bytes in the buffer */
#define GWBUF_CONSUME(b, bytes) ((b)->start = bytes > ((char *)(b)->end - (char *)(b)->start) ? (b)->end : (void *)((char *)(b)->start + (bytes)));

/*< Check if a given pointer is within the buffer */
#define GWBUF_POINTER_IN_BUFFER (ptr, b)\
    ((char *)(ptr) >= (char *)(b)->start && (char *)(ptr) < (char *)(b)->end)

/*< Consume a complete buffer */
#define GWBUF_CONSUME_ALL(b)    gwbuf_consume((b), GWBUF_LENGTH((b)))

#define GWBUF_RTRIM(b, bytes)\
    ((b)->end = bytes > ((char *)(b)->end - (char *)(b)->start) ? (b)->start : \
     (void *)((char *)(b)->end - (bytes)));

#define GWBUF_TYPE(b) (b)->gwbuf_type
/*<
 * Function prototypes for the API to maniplate the buffers
 */

/**
 * Allocate a new gateway buffer of specified size.
 *
 * @param size  The size in bytes of the data area required
 *
 * @return Pointer to the buffer structure or NULL if memory could not
 *         be allocated.
 */
extern GWBUF *gwbuf_alloc(unsigned int size);

/**
 * Allocate a new gateway buffer structure of specified size and load with data.
 *
 * @param size  The size in bytes of the data area required
 * @param data  Pointer to the data (size bytes) to be loaded
 *
 * @return Pointer to the buffer structure or NULL if memory could not
 *         be allocated.
 */
extern GWBUF *gwbuf_alloc_and_load(unsigned int size, const void *data);

/**
 * Free a chain of gateway buffers
 *
 * @param buf  The head of the list of buffers to free
 */
extern void gwbuf_free(GWBUF *buf);

/**
 * Clone a GWBUF. Note that if the GWBUF is actually a list of
 * GWBUFs, then every GWBUF in the list will be cloned. Note that but
 * for the GWBUF structure itself, the data is shared.
 *
 * @param buf  The GWBUF to be cloned.
 *
 * @return The cloned GWBUF, or NULL if @buf was NULL or if any part
 *         of @buf could not be cloned.
 */
extern GWBUF *gwbuf_clone(GWBUF *buf);

/**
 * Compare two GWBUFs. Two GWBUFs are considered identical if their
 * content is identical, irrespective of whether one is segmented and
 * the other is not.
 *
 * @param lhs  One GWBUF
 * @param rhs  Another GWBUF
 *
 * @return  0 if the content is identical,
 *         -1 if @c lhs is less than @c rhs, and
 *          1 if @c lhs is more than @c rhs.
 *
 * @attention A NULL @c GWBUF is considered to be less than a non-NULL one,
 *            and a shorter @c GWBUF less than a longer one. Otherwise the
 *            the sign of the return value is determined by the sign of the
 *            difference between the first pair of bytes (interpreted as
 *            unsigned char) that differ in lhs and rhs.
 */
extern int gwbuf_compare(const GWBUF* lhs, const GWBUF* rhs);

/**
 * Append a buffer onto a linked list of buffer structures.
 *
 * This call should be made with the caller holding the lock for the linked
 * list.
 *
 * @param head  The current head of the linked list
 * @param tail  The new buffer to make the tail of the linked list
 *
 * @return The new head of the linked list
 */
extern GWBUF *gwbuf_append(GWBUF *head, GWBUF *tail);

/**
 * @brief Consume data from buffer chain
 *
 * Data is consumed from @p head until either @p length bytes have been
 * processed or @p head is empty. If @p head points to a chain of buffers,
 * those buffers are counted as a part of @p head and will also be consumed if
 * @p length exceeds the size of the first buffer.
 *
 * @param head    The head of the linked list
 * @param length  Number of bytes to consume
 *
 * @return The head of the linked list or NULL if everything was consumed
 */
extern GWBUF *gwbuf_consume(GWBUF *head, unsigned int length);

/**
 * Trim bytes from the end of a GWBUF structure that may be the first
 * in a list. If the buffer has n_bytes or less then it will be freed and
 * the next buffer in the list will be returned, or if none, NULL.
 *
 * @param head     The buffer to trim
 * @param n_bytes  The number of bytes to trim off
 *
 * @return The buffer chain or NULL if buffer chain now empty
 */
extern GWBUF *gwbuf_rtrim(GWBUF *head, unsigned int length);

/**
 * Return the number of bytes of data in the linked list.
 *
 * @param head  The current head of the linked list
 *
 * @return The number of bytes of data in the linked list
 */
extern unsigned int gwbuf_length(const GWBUF *head);

/**
 * Return the number of individual buffers in the linked list.
 *
 * Currently not used, provided mainly for use during debugging sessions.
 *
 * @param head  The current head of the linked list
 *
 * @return The number of bytes of data in the linked list
 */
extern int gwbuf_count(const GWBUF *head);

/**
 * @brief Copy bytes from a buffer
 *
 * Copy bytes from a chain of buffers. Supports copying data from buffers where
 * the data is spread across multiple buffers.
 *
 * @param buffer  Buffer to copy from
 * @param offset  Offset into the buffer
 * @param bytes   Number of bytes to copy
 * @param dest    Destination where the bytes are copied
 *
 * @return Number of bytes copied.
 */
extern size_t gwbuf_copy_data(const GWBUF *buffer, size_t offset, size_t bytes, uint8_t* dest);

/**
 * @brief Split a buffer in two
 *
 * The returned value will be @c length bytes long. If the length of @c buf
 * exceeds @c length, the remaining buffers are stored in @buf.
 *
 * @param buf Buffer chain to split
 * @param length Number of bytes that the returned buffer should contain
 *
 * @return Head of the buffer chain.
 */
extern GWBUF *gwbuf_split(GWBUF **buf, size_t length);

/**
 * Set given type to all buffers on the list.
 * *
 * @param buf   The shared buffer
 * @param type  Type to be added
 */
extern void gwbuf_set_type(GWBUF *head, gwbuf_type_t type);

/**
 * Add a property to a buffer.
 *
 * @param buf    The buffer to add the property to
 * @param name   The property name
 * @param value  The property value
 *
 * @return True on success, false otherwise.
 */
extern bool gwbuf_add_property(GWBUF *buf, char *name, char *value);

/**
 * Return the value of a buffer property
 *
 * @param buf   The buffer itself
 * @param name  The name of the property to return
 *
 * @return The property value or NULL if the property was not found.
 */
extern char *gwbuf_get_property(GWBUF *buf, char *name);

/**
 * Convert a chain of GWBUF structures into a single GWBUF structure
 *
 * @param orig  The chain to convert
 *
 * @return NULL if @c buf is NULL or if a memory allocation fails,
 *         @c buf if @c buf already is contiguous, and otherwise
 *         a contigious copy of @c buf.
 *
 * @attention If a non-NULL value is returned, the @c buf should no
 *            longer be used as it may have been freed.
 */
extern GWBUF *gwbuf_make_contiguous(GWBUF *buf);

/**
 * Add hint to a buffer.
 *
 * @param buf   The buffer to add the hint to
 * @param hint  The hint. Note that the ownership of @c hint is transferred
 *              to @c buf.
 */
extern void gwbuf_add_hint(GWBUF *buf, HINT *hint);

/**
 * Add a buffer object to GWBUF buffer.
 *
 * @param buf         GWBUF where object is added
 * @param id          Type identifier for object
 * @param data        Object data
 * @param donefun_fp  Clean-up function to be executed before buffer is freed.
 */
void gwbuf_add_buffer_object(GWBUF* buf,
                             bufobj_id_t id,
                             void*  data,
                             void (*donefun_fp)(void *));

/**
 * Search buffer object which matches with the id.
 *
 * @param buf  GWBUF to be searched
 * @param id   Identifier for the object
 *
 * @return Searched buffer object or NULL if not found
 */
void *gwbuf_get_buffer_object_data(GWBUF* buf, bufobj_id_t id);
#if defined(BUFFER_TRACE)
extern void dprintAllBuffers(void *pdcb);
#endif

MXS_END_DECLS
