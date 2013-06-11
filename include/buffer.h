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

/*
 * Definitions relating the gateway buffer manipulation facilities.
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
 * Revision History
 *
 * Date		Who		Description
 * 10/06/13	Mark Riddoch	Initial implementation
 *
 */
typedef struct gwbuf {
	struct gwbuf	*next;			// Next buffer in a linked chain of buffers
	void		*start;			// Start of the valid data
	void		*end;			// First byte after the valid data
	unsigned char	*data;			// Physical memory that was allocated
} GWBUF;

/*
 * Macros to access the data in the buffers
 */
#define GWBUF_DATA(b)		((b)->start)
#define GWBUF_LENGTH(b)		((b)->end - (b)->start)
#define GWBUF_EMPTY(b)		((b)->start == (b)->end)
#define GWBUF_CONSUME(b, bytes)	(b)->start += bytes

/*
 * Function prototypes for the API to maniplate the buffers
 */
extern GWBUF	*gwbuf_alloc(unsigned int size);
extern void	gwbuf_free(GWBUF *buf);
extern GWBUF	*gwbuf_append(GWBUF *head, GWBUF *tail);
extern GWBUF	*gwbuf_consume(GWBUF *head, unsigned int length);
extern unsigned int	gwbuf_length(GWBUF *head);


#endif
