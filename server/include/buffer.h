#ifndef _BUFFER_H
#define _BUFFER_H
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
 * Copyright MariaDB Corporation Ab 2013-2014
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
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 10/06/2013   Mark Riddoch            Initial implementation
 * 11/07/2013   Mark Riddoch            Addition of reference count in the gwbuf
 * 16/07/2013   Massimiliano Pinto      Added command type for the queue
 * 10/07/2014   Mark Riddoch            Addition of hints
 * 15/07/2014   Mark Riddoch            Added buffer properties
 * 03/10/2014   Martin Brampton         Pointer arithmetic standard conformity
 *                                      Add more buffer handling macros
 *                                      Add gwbuf_rtrim (handle chains)
 * 09/11/2014   Martin Brampton         Add dprintAllBuffers (conditional compilation)
 *
 * @endverbatim
 */
#include <string.h>
#include <skygw_debug.h>
#include <hint.h>
#include <spinlock.h>
#include <stdint.h>

EXTERN_C_BLOCK_BEGIN

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

/**
 * A structure to encapsulate the data in a form that the data itself can be
 * shared between multiple GWBUF's without the need to make multiple copies
 * but still maintain separate data pointers.
 */
typedef struct
{
    unsigned char   *data;                  /*< Physical memory that was allocated */
    int             refcount;               /*< Reference count on the buffer */
} SHARED_BUF;

typedef enum
{
    GWBUF_INFO_NONE         = 0x0,
    GWBUF_INFO_PARSED       = 0x1
} gwbuf_info_t;

#define GWBUF_IS_PARSED(b)      (b->gwbuf_info & GWBUF_INFO_PARSED)

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
    buffer_object_t *gwbuf_bufobj; /*< List of objects referred to by GWBUF */
    gwbuf_info_t    gwbuf_info; /*< Info bits */
    gwbuf_type_t    gwbuf_type; /*< buffer's data type information */
    HINT            *hint;  /*< Hint data for this buffer */
    BUF_PROPERTY    *properties; /*< Buffer properties */
} GWBUF;

/*<
 * Macros to access the data in the buffers
 */
/*< First valid, unconsumed byte in the buffer */
#define GWBUF_DATA(b)           ((b)->start)

/*< Number of bytes in the individual buffer */
#define GWBUF_LENGTH(b)         ((char *)(b)->end - (char *)(b)->start)

/*< Return the byte at offset byte from the start of the unconsumed portion of the buffer */
#define GWBUF_DATA_CHAR(b, byte)    (GWBUF_LENGTH(b) < ((byte)+1) ? -1 : *(((char *)(b)->start)+4))

/*< Check that the data in a buffer has the SQL marker*/
#define GWBUF_IS_SQL(b)         (0x03 == GWBUF_DATA_CHAR(b,4))

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
extern GWBUF            *gwbuf_alloc(unsigned int size);
extern void             gwbuf_free(GWBUF *buf);
extern GWBUF            *gwbuf_clone(GWBUF *buf);
extern GWBUF            *gwbuf_append(GWBUF *head, GWBUF *tail);
extern GWBUF            *gwbuf_consume(GWBUF *head, unsigned int length);
extern GWBUF            *gwbuf_trim(GWBUF *head, unsigned int length);
extern GWBUF            *gwbuf_rtrim(GWBUF *head, unsigned int length);
extern unsigned int     gwbuf_length(GWBUF *head);
extern GWBUF            *gwbuf_clone_portion(GWBUF *head, size_t offset, size_t len);
extern GWBUF            *gwbuf_clone_transform(GWBUF *head, gwbuf_type_t type);
extern GWBUF            *gwbuf_clone_all(GWBUF* head);
extern void             gwbuf_set_type(GWBUF *head, gwbuf_type_t type);
extern int              gwbuf_add_property(GWBUF *buf, char *name, char *value);
extern char             *gwbuf_get_property(GWBUF *buf, char *name);
extern GWBUF            *gwbuf_make_contiguous(GWBUF *);
extern int              gwbuf_add_hint(GWBUF *, HINT *);

void                    gwbuf_add_buffer_object(GWBUF* buf,
                                                bufobj_id_t id,
                                                void*  data,
                                                void (*donefun_fp)(void *));
void*                   gwbuf_get_buffer_object_data(GWBUF* buf, bufobj_id_t id);
#if defined(BUFFER_TRACE)
extern void             dprintAllBuffers(void *pdcb);
#endif
EXTERN_C_BLOCK_END


#endif
