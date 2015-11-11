/*
 * This file is distributed as part of MaxScale.  It is free
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
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 29-08-2014	Martin Brampton		Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <buffer.h>
#include <hint.h>

/**
 * test1	Allocate a buffer and do lots of things
 *
 */
static int
test1()
{
GWBUF   *buffer, *extra, *clone, *partclone, *transform;
HINT    *hint;
int     size = 100;
int     bite1 = 35;
int     bite2 = 60;
int     bite3 = 10;
int     buflen;

        /* Single buffer tests */
        ss_dfprintf(stderr,
                    "testbuffer : creating buffer with data size %d bytes",
                    size); 
        buffer = gwbuf_alloc(size);
        ss_dfprintf(stderr, "\t..done\nAllocated buffer of size %d.", size);
        buflen = GWBUF_LENGTH(buffer);
        ss_dfprintf(stderr, "\nBuffer length is now %d", buflen);
        ss_info_dassert(size == buflen, "Incorrect buffer size");
        ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
        ss_info_dassert(GWBUF_IS_TYPE_UNDEFINED(buffer), "Buffer type should be undefined");
        ss_dfprintf(stderr, "\t..done\nSet a hint for the buffer");
        hint = hint_create_parameter(NULL, "name", "value");
        gwbuf_add_hint(buffer, hint);
        ss_info_dassert(hint == buffer->hint, "Buffer should point to first and only hint");
        ss_dfprintf(stderr, "\t..done\nSet a property for the buffer");
        gwbuf_add_property(buffer, "name", "value");
        ss_info_dassert(0 == strcmp("value", gwbuf_get_property(buffer, "name")), "Should now have correct property");
        strcpy(GWBUF_DATA(buffer), "The quick brown fox jumps over the lazy dog");
        ss_dfprintf(stderr, "\t..done\nLoad some data into the buffer");
        ss_info_dassert('q' == GWBUF_DATA_CHAR(buffer, 4), "Fourth character of buffer must be 'q'");
        ss_info_dassert(-1 == GWBUF_DATA_CHAR(buffer, 105), "Hundred and fifth character of buffer must return -1");
        ss_info_dassert(0 == GWBUF_IS_SQL(buffer), "Must say buffer is not SQL, as it does not have marker");
        strcpy(GWBUF_DATA(buffer), "1234\x03SELECT * FROM sometable");
        ss_dfprintf(stderr, "\t..done\nLoad SQL data into the buffer");
        ss_info_dassert(1 == GWBUF_IS_SQL(buffer), "Must say buffer is SQL, as it does have marker");
        transform = gwbuf_clone_transform(buffer, GWBUF_TYPE_PLAINSQL);
        ss_dfprintf(stderr, "\t..done\nAttempt to transform buffer to plain SQL - should fail");
        ss_info_dassert(NULL == transform, "Buffer cannot be transformed to plain SQL");
        gwbuf_set_type(buffer, GWBUF_TYPE_MYSQL);
        ss_dfprintf(stderr, "\t..done\nChanged buffer type to MySQL");
        ss_info_dassert(GWBUF_IS_TYPE_MYSQL(buffer), "Buffer type changed to MySQL");
        transform = gwbuf_clone_transform(buffer, GWBUF_TYPE_PLAINSQL);
        ss_dfprintf(stderr, "\t..done\nAttempt to transform buffer to plain SQL - should succeed");
        ss_info_dassert((NULL != transform) && (GWBUF_IS_TYPE_PLAINSQL(transform)), "Transformed buffer is plain SQL");
        clone = gwbuf_clone(buffer);
        ss_dfprintf(stderr, "\t..done\nCloned buffer");
        buflen = GWBUF_LENGTH(clone);
        ss_dfprintf(stderr, "\nCloned buffer length is now %d", buflen);
        ss_info_dassert(size == buflen, "Incorrect buffer size");
        ss_info_dassert(0 == GWBUF_EMPTY(clone), "Cloned buffer should not be empty");
        ss_dfprintf(stderr, "\t..done\n");
        gwbuf_free(clone);
        ss_dfprintf(stderr, "Freed cloned buffer");
        ss_dfprintf(stderr, "\t..done\n");
        partclone = gwbuf_clone_portion(buffer, 25, 50);
        buflen = GWBUF_LENGTH(partclone);
        ss_dfprintf(stderr, "Part cloned buffer length is now %d", buflen);
        ss_info_dassert(50 == buflen, "Incorrect buffer size");
        ss_info_dassert(0 == GWBUF_EMPTY(partclone), "Part cloned buffer should not be empty");
        ss_dfprintf(stderr, "\t..done\n");
        gwbuf_free(partclone);
        ss_dfprintf(stderr, "Freed part cloned buffer");
        ss_dfprintf(stderr, "\t..done\n");
        buffer = gwbuf_consume(buffer, bite1);
        ss_info_dassert(NULL != buffer, "Buffer should not be null");
        buflen = GWBUF_LENGTH(buffer);
        ss_dfprintf(stderr, "Consumed %d bytes, now have %d, should have %d", bite1, buflen, size-bite1);
        ss_info_dassert((size - bite1) == buflen, "Incorrect buffer size");
        ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
        ss_dfprintf(stderr, "\t..done\n");
        buffer = gwbuf_consume(buffer, bite2);
		ss_info_dassert(NULL != buffer, "Buffer should not be null");
        buflen = GWBUF_LENGTH(buffer);
        ss_dfprintf(stderr, "Consumed %d bytes, now have %d, should have %d", bite2, buflen, size-bite1-bite2);
        ss_info_dassert((size-bite1-bite2) == buflen, "Incorrect buffer size");
        ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
        ss_dfprintf(stderr, "\t..done\n");
        buffer = gwbuf_consume(buffer, bite3);
        ss_dfprintf(stderr, "Consumed %d bytes, should have null buffer", bite3);
        ss_info_dassert(NULL == buffer, "Buffer should be null");
        
        /* Buffer list tests */
        size = 100000;
        buffer = gwbuf_alloc(size);
        ss_dfprintf(stderr, "\t..done\nAllocated buffer of size %d.", size);
        buflen = GWBUF_LENGTH(buffer);
        ss_dfprintf(stderr, "\nBuffer length is now %d", buflen);
        ss_info_dassert(size == buflen, "Incorrect buffer size");
        ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
        ss_info_dassert(GWBUF_IS_TYPE_UNDEFINED(buffer), "Buffer type should be undefined");
        extra = gwbuf_alloc(size);
        buflen = GWBUF_LENGTH(buffer);
        ss_dfprintf(stderr, "\t..done\nAllocated extra buffer of size %d.", size);
        ss_info_dassert(size == buflen, "Incorrect buffer size");
        buffer = gwbuf_append(buffer, extra);
        buflen = gwbuf_length(buffer);
        ss_dfprintf(stderr, "\t..done\nAppended extra buffer to original buffer to create list of size %d", buflen);
        ss_info_dassert((size*2) == gwbuf_length(buffer), "Incorrect size for set of buffers");
        buffer = gwbuf_rtrim(buffer, 60000);
        buflen = GWBUF_LENGTH(buffer);
        ss_dfprintf(stderr, "\t..done\nTrimmed 60 bytes from buffer, now size is %d.", buflen);
        ss_info_dassert((size-60000) == buflen, "Incorrect buffer size");
        buffer = gwbuf_rtrim(buffer, 60000);
        buflen = GWBUF_LENGTH(buffer);
        ss_dfprintf(stderr, "\t..done\nTrimmed another 60 bytes from buffer, now size is %d.", buflen);
        ss_info_dassert(100000 == buflen, "Incorrect buffer size");
        ss_info_dassert(buffer == extra, "The buffer pointer should now point to the extra buffer");
        ss_dfprintf(stderr, "\t..done\n");

        /** gwbuf_clone_all test  */
        size_t headsize = 10;
        GWBUF* head = gwbuf_alloc(headsize);
        size_t tailsize = 20;
        GWBUF* tail = gwbuf_alloc(tailsize);

        ss_info_dassert(head && tail, "Head and tail buffers should both be non-NULL");
        GWBUF* append = gwbuf_append(head, tail);
        ss_info_dassert(append == head, "gwbuf_append should return head");
        ss_info_dassert(append->next == tail, "After append tail should be in the next pointer of head");
        ss_info_dassert(append->tail == tail, "After append tail should be in the tail pointer of head");
        GWBUF* all_clones = gwbuf_clone_all(head);
        ss_info_dassert(all_clones && all_clones->next, "Cloning all should work");
        ss_info_dassert(GWBUF_LENGTH(all_clones) == headsize, "First buffer should be 10 bytes");
        ss_info_dassert(GWBUF_LENGTH(all_clones->next) == tailsize, "Second buffer should be 20 bytes");
        ss_info_dassert(gwbuf_length(all_clones) == headsize + tailsize, "Total buffer length should be 30 bytes");

	return 0;
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}


