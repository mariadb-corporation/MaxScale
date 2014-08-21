#ifndef _BUFFER_H
#define _BUFFER_H
/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
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
 * Date		Who			Description
 * 10/06/2013	Mark Riddoch		Initial implementation
 * 11/07/2013	Mark Riddoch		Addition of reference count in the gwbuf
 * 16/07/2013	Massimiliano Pinto	Added command type for the queue
 *
 * @endverbatim
 */
#include <skygw_debug.h>

EXTERN_C_BLOCK_BEGIN

typedef enum 
{
        GWBUF_TYPE_UNDEFINED       = 0x00,
        GWBUF_TYPE_PLAINSQL        = 0x01,
        GWBUF_TYPE_MYSQL           = 0x02,
        GWBUF_TYPE_SINGLE_STMT     = 0x04,
        GWBUF_TYPE_SESCMD_RESPONSE = 0x08,
	GWBUF_TYPE_RESPONSE_END    = 0x10,
        GWBUF_TYPE_SESCMD          = 0x20
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
typedef struct  {
	unsigned char	*data;			/*< Physical memory that was allocated */
	int		refcount;		/*< Reference count on the buffer */
} SHARED_BUF;


typedef struct parsing_info_st {
#if defined(SS_DEBUG)
        skygw_chk_t pi_chk_top;     
#endif
        void* pi_handle;                        /*< parsing info object pointer */
        char* pi_query_plain_str;               /*< query as plain string */
        void (*pi_done_fp)(void *);             /*< clean-up function for parsing info */
#if defined(SS_DEBUG)
        skygw_chk_t pi_chk_tail;
#endif
} parsing_info_t;


/**
 * The buffer structure used by the descriptor control blocks.
 *
 * Linked lists of buffers are created as data is read from a descriptor
 * or written to a descriptor. The use of linked lists of buffers with
 * flexible data pointers is designed to minimise the need for data to
 * be copied within the gateway.
 */
typedef struct gwbuf {
	struct gwbuf	*next;	/*< Next buffer in a linked chain of buffers */
	void		*start;	/*< Start of the valid data */
	void		*end;	/*< First byte after the valid data */
	SHARED_BUF	*sbuf;  /*< The shared buffer with the real data */
	void            *gwbuf_parsing_info; /*< parsing info object pointer */
	gwbuf_type_t    gwbuf_type; /*< buffer's data type information */
} GWBUF;

/*<
 * Macros to access the data in the buffers
 */
/*< First valid, uncomsumed byte in the buffer */
#define GWBUF_DATA(b)		((b)->start)

/*< Number of bytes in the individual buffer */
#define GWBUF_LENGTH(b)		((b)->end - (b)->start)

/*< True if all bytes in the buffer have been consumed */
#define GWBUF_EMPTY(b)		((b)->start == (b)->end)

/*< Consume a number of bytes in the buffer */
#define GWBUF_CONSUME(b, bytes)	(b)->start += (bytes)

#define GWBUF_RTRIM(b, bytes)	(b)->end -= (bytes)

#define GWBUF_TYPE(b) (b)->gwbuf_type
/*<
 * Function prototypes for the API to maniplate the buffers
 */
extern GWBUF		*gwbuf_alloc(unsigned int size);
extern void		gwbuf_free(GWBUF *buf);
extern GWBUF		*gwbuf_clone(GWBUF *buf);
extern GWBUF		*gwbuf_append(GWBUF *head, GWBUF *tail);
extern GWBUF		*gwbuf_consume(GWBUF *head, unsigned int length);
extern GWBUF		*gwbuf_trim(GWBUF *head, unsigned int length);
extern unsigned int	gwbuf_length(GWBUF *head);
extern GWBUF            *gwbuf_clone_portion(GWBUF *head, size_t offset, size_t len);
extern GWBUF            *gwbuf_clone_transform(GWBUF *head, gwbuf_type_t type);
extern void             gwbuf_set_type(GWBUF *head, gwbuf_type_t type);
void*                   gwbuf_get_parsing_info(GWBUF* buf);


EXTERN_C_BLOCK_END


#endif
