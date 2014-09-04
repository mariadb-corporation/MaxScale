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
 * Date		Who			Description
 * 10/06/13	Mark Riddoch		Initial implementation
 * 11/07/13	Mark Riddoch		Add reference count mechanism
 * 16/07/2013	Massimiliano Pinto	Added command type to gwbuf struct
 * 24/06/2014	Mark Riddoch		Addition of gwbuf_trim
 * 15/07/2014	Mark Riddoch		Addition of properties
 *
 * @endverbatim
 */
#include <stdlib.h>
#include <buffer.h>
#include <atomic.h>
#include <skygw_debug.h>

static buffer_object_t* gwbuf_remove_buffer_object(
        GWBUF*           buf,
        buffer_object_t* bufobj);


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
GWBUF		*rval;
SHARED_BUF	*sbuf;

	// Allocate the buffer header
	if ((rval = (GWBUF *)malloc(sizeof(GWBUF))) == NULL)
	{
		return NULL;
	}

	// Allocate the shared data buffer
	if ((sbuf = (SHARED_BUF *)malloc(sizeof(SHARED_BUF))) == NULL)
	{
		free(rval);
		return NULL;
	}

	// Allocate the space for the actual data
	if ((sbuf->data = (unsigned char *)malloc(size)) == NULL)
	{
		free(rval);
		free(sbuf);
		return NULL;
	}
	spinlock_init(&rval->gwbuf_lock);
	rval->start = sbuf->data;
	rval->end = rval->start + size;
	sbuf->refcount = 1;
	rval->sbuf = sbuf;
	rval->next = NULL;
	rval->hint = NULL;
	rval->properties = NULL;
        rval->gwbuf_type = GWBUF_TYPE_UNDEFINED;
        rval->gwbuf_info = GWBUF_INFO_NONE;
        rval->gwbuf_bufobj = NULL;
        CHK_GWBUF(rval);
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
BUF_PROPERTY	*prop;

        buffer_object_t* bo;
        
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
GWBUF	*rval;

	if ((rval = (GWBUF *)malloc(sizeof(GWBUF))) == NULL)
	{
		return NULL;
	}

	atomic_add(&buf->sbuf->refcount, 1);
	rval->sbuf = buf->sbuf;
	rval->start = buf->start;
	rval->end = buf->end;
        rval->gwbuf_type = buf->gwbuf_type;
	rval->properties = NULL;
        rval->hint = NULL;
        rval->gwbuf_info = buf->gwbuf_info;
        rval->gwbuf_bufobj = buf->gwbuf_bufobj;
	rval->next = NULL;
        CHK_GWBUF(rval);
	return rval;
}


GWBUF *gwbuf_clone_portion(
        GWBUF *buf,
        size_t start_offset,
        size_t length)
{
        GWBUF* clonebuf;
        
        CHK_GWBUF(buf);
        ss_dassert(start_offset+length <= GWBUF_LENGTH(buf));
        
        if ((clonebuf = (GWBUF *)malloc(sizeof(GWBUF))) == NULL)
        {
                return NULL;
        }
        atomic_add(&buf->sbuf->refcount, 1);
        clonebuf->sbuf = buf->sbuf;
        clonebuf->gwbuf_type = buf->gwbuf_type; /*< clone info bits too */
        clonebuf->start = (void *)((char*)buf->start)+start_offset;
        clonebuf->end = (void *)((char *)clonebuf->start)+length;
        clonebuf->gwbuf_type = buf->gwbuf_type; /*< clone the type for now */ 
	clonebuf->properties = NULL;
        clonebuf->hint = NULL;
        clonebuf->gwbuf_info = buf->gwbuf_info;
        clonebuf->gwbuf_bufobj = buf->gwbuf_bufobj;
        clonebuf->next = NULL;
        CHK_GWBUF(clonebuf);
        return clonebuf;
        
}

/**
 * Returns pointer to GWBUF of a requested type.
 * As of 10.3.14 only MySQL to plain text conversion is supported.
 * Return NULL if conversion between types is not supported or due lacking
 * type information.
 */
GWBUF *gwbuf_clone_transform(
        GWBUF *      head, 
        gwbuf_type_t targettype)
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
                        clonebuf = gwbuf_clone_portion(
                                        head, 
                                        5, 
                                        GWBUF_LENGTH(head)-5);                                
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
        CHK_GWBUF(head);
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
        CHK_GWBUF(head);
	GWBUF_CONSUME(head, length);
        CHK_GWBUF(head);
        
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
 * @param buf		The buffer to trim
 * @param n_bytes	The number of bytes to trim off
 * @return 		The buffer chain or NULL if buffer has <= n_bytes
 */
GWBUF *
gwbuf_trim(GWBUF *buf, unsigned int n_bytes)
{
	if (GWBUF_LENGTH(buf) <= n_bytes)
	{
		gwbuf_consume(buf, GWBUF_LENGTH(buf));
		return NULL;
	}
	buf->end -= n_bytes;

	return buf;
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
void gwbuf_add_buffer_object(
        GWBUF* buf,
        bufobj_id_t id,
        void*  data,
        void (*donefun_fp)(void *))
{
        buffer_object_t** p_b;
        buffer_object_t*  newb;        
        
        CHK_GWBUF(buf);
        newb = (buffer_object_t *)malloc(sizeof(buffer_object_t));
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
void* gwbuf_get_buffer_object_data(
        GWBUF*      buf, 
        bufobj_id_t id)
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
        
        return bo->bo_data;
}

/**
 * @return pointer to next buffer object or NULL
 */
static buffer_object_t* gwbuf_remove_buffer_object(
	GWBUF*           buf,
	buffer_object_t* bufobj)
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
 * @param buf	The buffer to add the property to
 * @param name	The property name
 * @param value	The property value
 * @return	Non-zero on success
 */
int
gwbuf_add_property(GWBUF *buf, char *name, char *value)
{
BUF_PROPERTY	*prop;

	if ((prop = malloc(sizeof(BUF_PROPERTY))) == NULL)
		return 0;

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
 * @param buf	The buffer itself
 * @param name	The name of the property to return
 * @return The property value or NULL if the property was not found.
 */
char *
gwbuf_get_property(GWBUF *buf, char *name)
{
BUF_PROPERTY	*prop;

	spinlock_acquire(&buf->gwbuf_lock);
	prop = buf->properties;
	while (prop && strcmp(prop->name, name) != 0)
		prop = prop->next;
	spinlock_release(&buf->gwbuf_lock);
	if (prop)
		return prop->value;
	return NULL;
}


/**
 * Convert a chain of GWBUF structures into a single GWBUF structure
 *
 * @param orig		The chain to convert
 * @return		The contiguous buffer
 */
GWBUF *
gwbuf_make_contiguous(GWBUF *orig)
{
GWBUF	*newbuf;
char	*ptr;
int	len;

	if (orig->next == NULL)
		return orig;

	if ((newbuf = gwbuf_alloc(gwbuf_length(orig))) != NULL)
	{
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
 * @param buf	The buffer to add the hint to
 * @param hint	The hint itself
 * @return	Non-zero on success
 */
int
gwbuf_add_hint(GWBUF *buf, HINT *hint)
{
HINT	*ptr; 

	spinlock_acquire(&buf->gwbuf_lock);
	if (buf->hint)
	{
		ptr = buf->hint;
		while (ptr->next)
			ptr = ptr->next;
		ptr->next = hint;
	}
	else
	{
		buf->hint = hint;
	}
	spinlock_release(&buf->gwbuf_lock);
	return 1;
}
