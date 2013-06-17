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
 * @file buffer.h  - The Gateway buffer management functions
 *
 * The buffer management is based on the principle of a linked list
 * of variable size buffer, the intention beign to allow longer
 * content to be buffered in a list and minimise any need to copy
 * data between buffers.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 10/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <stdlib.h>
#include <buffer.h>


/**
 * Allocate a new gateway buffer structure of size bytes.
 *
 * For now we allocate memory directly from malloc for buffer the management
 * structure and the actual data buffer itself. We may swap at a future date
 * to a more effecient mechanism.
 *
 * @param	size The size in bytes of the data area required
 * @return	Pointer to the buffer structure or NULL if memory could not
 *		be allocated.
 */
GWBUF	*
gwbuf_alloc(unsigned int size)
{
GWBUF	*rval;

	// Allocate the buffer header
	if ((rval = (GWBUF *)malloc(sizeof(GWBUF))) == NULL)
	{
		return NULL;
	}

	// Allocate the space for the actual data
	if ((rval->data = (unsigned char *)malloc(size)) == NULL)
	{
		free(rval);
		return NULL;
	}
	rval->start = rval->data;
	rval->end = rval->start + size;
	rval->next = NULL;

	return rval;
}

/**
 * Free a gateway buffer
 *
 * @param buf The buffer to free
 */
void
gwbuf_free(GWBUF *buf)
{
	free(buf->data);
	free(buf);
}

/**
 * Append a buffer onto a linked list of buffer structures.
 *
 * This call should be made with the caller holding the lock for the linked
 * list.
 *
 * @param head	The current head of the linked list
 * @param tail	The new buffer to make the tail of the linked list
 * @return	The new head of the linked list
 */
GWBUF	*
gwbuf_append(GWBUF *head, GWBUF *tail)
{
GWBUF	*ptr = head;

	if (!head)
		return tail;
	while (ptr->next)
	{
		ptr = ptr->next;
	}
	ptr->next = tail;
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
 * @param head		The head of the linked list
 * @param length	The amount of data to consume
 * @return The head of the linked list
 */
GWBUF *
gwbuf_consume(GWBUF *head, unsigned int length)
{
GWBUF *rval = head;

	GWBUF_CONSUME(head, length);
	if (GWBUF_EMPTY(head))
	{
		rval = head->next;
		gwbuf_free(head);
	}
	return rval;
}

/**
 * Return the number of bytes of data in the linked list.
 *
 * @param head	The current head of the linked list
 * @return The number of bytes of data in the linked list
 */
unsigned int
gwbuf_length(GWBUF *head)
{
int	rval = 0;

	while (head)
	{
		rval += GWBUF_LENGTH(head);
		head = head->next;
	}
	return rval;
}
