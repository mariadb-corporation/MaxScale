/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 29-08-2014   Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when building in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <maxscale/alloc.h>
#include <maxscale/buffer.h>
#include <maxscale/hint.h>

/**
 * Generate predefined test data
 *
 * @param count Number of bytes to generate
 * @return Pointer to @c count bytes of data
 */
uint8_t* generate_data(size_t count)
{
    uint8_t* data = (uint8_t*)MXS_MALLOC(count);
    MXS_ABORT_IF_NULL(data);

    srand(0);

    for (size_t i = 0; i < count; i++)
    {
        data[i] = rand() % 256;
    }

    return data;
}

size_t buffers[] =
{
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67,
    71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149
};

const int n_buffers = sizeof(buffers) / sizeof(size_t);

GWBUF* create_test_buffer()
{
    GWBUF* head = NULL;
    size_t total = 0;

    for (int i = 0; i < n_buffers; i++)
    {
        total += buffers[i];
    }

    uint8_t* data = generate_data(total);
    total = 0;

    for (size_t i = 0; i < sizeof(buffers) / sizeof(size_t); i++)
    {
        head = gwbuf_append(head, gwbuf_alloc_and_load(buffers[i], data + total));
        total += buffers[i];
    }

    MXS_FREE(data);

    return head;
}

int get_length_at(int n)
{
    int total = 0;

    for (int i = 0; i < n_buffers && i <= n; i++)
    {
        total += buffers[i];
    }

    return total;
}

void split_buffer(int n, int offset)
{
    size_t cutoff = get_length_at(n) + offset;
    GWBUF* buffer = create_test_buffer();
    int len = gwbuf_length(buffer);
    GWBUF* newbuf = gwbuf_split(&buffer, cutoff);

    ss_info_dassert(buffer && newbuf, "Both buffers should be non-NULL");
    ss_info_dassert(gwbuf_length(newbuf) == cutoff, "New buffer should be have correct length");
    ss_info_dassert(gwbuf_length(buffer) == len - cutoff, "Old buffer should be have correct length");
    gwbuf_free(buffer);
    gwbuf_free(newbuf);
}


void consume_buffer(int n, int offset)
{
    size_t cutoff = get_length_at(n) + offset;
    GWBUF* buffer = create_test_buffer();
    int len = gwbuf_length(buffer);
    buffer = gwbuf_consume(buffer, cutoff);

    ss_info_dassert(buffer, "Buffer should be non-NULL");
    ss_info_dassert(gwbuf_length(buffer) == len - cutoff, "Buffer should be have correct length");
    gwbuf_free(buffer);
}

void copy_buffer(int n, int offset)
{
    size_t cutoff = get_length_at(n) + offset;
    uint8_t* data = generate_data(cutoff);
    GWBUF* buffer = create_test_buffer();
    int len = gwbuf_length(buffer);
    uint8_t dest[cutoff];

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(buffer, 0, cutoff, dest) == cutoff, "All bytes should be read");
    ss_info_dassert(memcmp(data, dest, sizeof(dest)) == 0, "Data should be OK");
    gwbuf_free(buffer);
    MXS_FREE(data);
}

/** gwbuf_split test - These tests assume allocation will always succeed */
void test_split()
{
    size_t headsize = 10;
    size_t tailsize = 20;

    GWBUF* oldchain = gwbuf_append(gwbuf_alloc(headsize), gwbuf_alloc(tailsize));
    ss_info_dassert(gwbuf_length(oldchain) == headsize + tailsize, "Allocated buffer should be 30 bytes");
    GWBUF* newchain = gwbuf_split(&oldchain, headsize + 5);
    ss_info_dassert(newchain && oldchain, "Both chains should be non-NULL");
    ss_info_dassert(gwbuf_length(newchain) == headsize + 5, "New chain should be 15 bytes long");
    ss_info_dassert(GWBUF_LENGTH(newchain) == headsize && GWBUF_LENGTH(newchain->next) == 5,
                    "The new chain should have a 10 byte buffer and a 5 byte buffer");
    ss_info_dassert(gwbuf_length(oldchain) == tailsize - 5, "Old chain should be 15 bytes long");
    ss_info_dassert(GWBUF_LENGTH(oldchain) == tailsize - 5 && oldchain->next == NULL,
                    "The old chain should have a 15 byte buffer and no next buffer");
    gwbuf_free(oldchain);
    gwbuf_free(newchain);

    oldchain = gwbuf_append(gwbuf_alloc(headsize), gwbuf_alloc(tailsize));
    newchain = gwbuf_split(&oldchain, headsize);
    ss_info_dassert(gwbuf_length(newchain) == headsize, "New chain should be 10 bytes long");
    ss_info_dassert(gwbuf_length(oldchain) == tailsize, "Old chain should be 20 bytes long");
    ss_info_dassert(oldchain->tail == oldchain, "Old chain tail should point to old chain");
    ss_info_dassert(oldchain->next == NULL, "Old chain should not have next buffer");
    ss_info_dassert(newchain->tail == newchain, "Old chain tail should point to old chain");
    ss_info_dassert(newchain->next == NULL, "new chain should not have next buffer");
    gwbuf_free(oldchain);
    gwbuf_free(newchain);

    oldchain = gwbuf_append(gwbuf_alloc(headsize), gwbuf_alloc(tailsize));
    newchain = gwbuf_split(&oldchain, headsize + tailsize);
    ss_info_dassert(newchain, "New chain should be non-NULL");
    ss_info_dassert(gwbuf_length(newchain) == headsize + tailsize, "New chain should be 30 bytes long");
    ss_info_dassert(oldchain == NULL, "Old chain should be NULL");
    gwbuf_free(newchain);

    /** Splitting of contiguous memory */
    GWBUF* buffer = gwbuf_alloc(10);
    GWBUF* newbuf = gwbuf_split(&buffer, 5);
    ss_info_dassert(buffer != newbuf, "gwbuf_split should return different pointers");
    ss_info_dassert(gwbuf_length(buffer) == 5 && GWBUF_LENGTH(buffer) == 5, "Old buffer should be 5 bytes");
    ss_info_dassert(gwbuf_length(newbuf) == 5 && GWBUF_LENGTH(newbuf) == 5, "New buffer should be 5 bytes");
    ss_info_dassert(buffer->tail == buffer, "Old buffer's tail should point to itself");
    ss_info_dassert(newbuf->tail == newbuf, "New buffer's tail should point to itself");
    ss_info_dassert(buffer->next == NULL, "Old buffer's next pointer should be NULL");
    ss_info_dassert(newbuf->next == NULL, "New buffer's next pointer should be NULL");
    gwbuf_free(buffer);
    gwbuf_free(newbuf);

    /** Bad parameter tests */
    GWBUF* ptr = NULL;
    ss_info_dassert(gwbuf_split(NULL, 0) == NULL, "gwbuf_split with NULL parameter should return NULL");
    ss_info_dassert(gwbuf_split(&ptr, 0) == NULL, "gwbuf_split with pointer to a NULL value should return NULL");
    buffer = gwbuf_alloc(10);
    ss_info_dassert(gwbuf_split(&buffer, 0) == NULL, "gwbuf_split with length of 0 should return NULL");
    ss_info_dassert(gwbuf_length(buffer) == 10, "Buffer should be 10 bytes");
    gwbuf_free(buffer);

    /** Splitting near buffer boudaries */
    for (int i = 0; i < n_buffers - 1; i++)
    {
        split_buffer(i, -1);
        split_buffer(i, 0);
        split_buffer(i, 1);
    }

    /** Split near last buffer's end */
    split_buffer(n_buffers - 1, -1);
}

/** gwbuf_alloc_and_load and gwbuf_copy_data tests */
void test_load_and_copy()
{
    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dest[8];
    GWBUF* head = gwbuf_alloc_and_load(4, data);
    GWBUF* tail = gwbuf_alloc_and_load(4, data + 4);

    ss_info_dassert(memcmp(GWBUF_DATA(head), data, 4) == 0, "Loading 4 bytes should succeed");
    ss_info_dassert(memcmp(GWBUF_DATA(tail), data + 4, 4) == 0, "Loading 4 bytes should succeed");

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(head, 0, 4, dest) == 4, "Copying 4 bytes should succeed");
    ss_info_dassert(memcmp(dest, data, 4) == 0, "Copied data should be from 1 to 4");

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(tail, 0, 4, dest) == 4, "Copying 4 bytes should succeed");
    ss_info_dassert(memcmp(dest, data + 4, 4) == 0, "Copied data should be from 5 to 8");
    head = gwbuf_append(head, tail);

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(head, 0, 8, dest) == 8, "Copying 8 bytes should succeed");
    ss_info_dassert(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(head, 4, 4, dest) == 4, "Copying 4 bytes at offset 4 should succeed");
    ss_info_dassert(memcmp(dest, data + 4, 4) == 0, "Copied data should be from 5 to 8");

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(head, 2, 4, dest) == 4, "Copying 4 bytes at offset 2 should succeed");
    ss_info_dassert(memcmp(dest, data + 2, 4) == 0, "Copied data should be from 5 to 8");

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(head, 0, 10, dest) == 8, "Copying 10 bytes should only copy 8 bytes");
    ss_info_dassert(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(head, 0, 0, dest) == 0, "Copying 0 bytes should not copy any bytes");

    memset(dest, 0, sizeof(dest));
    ss_info_dassert(gwbuf_copy_data(head, 0, -1, dest) == sizeof(data),
                    "Copying -1 bytes should copy all available data (cast to unsigned)");
    ss_info_dassert(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    ss_info_dassert(gwbuf_copy_data(head, -1, -1, dest) == 0,
                    "Copying -1 bytes at an offset of -1 should not copy any bytes");
    ss_info_dassert(gwbuf_copy_data(head, -1, 0, dest) == 0,
                    "Copying 0 bytes at an offset of -1 should not copy any bytes");
    gwbuf_free(head);

    /** Copying near buffer boudaries */
    for (int i = 0; i < n_buffers - 1; i++)
    {
        copy_buffer(i, -1);
        copy_buffer(i, 0);
        copy_buffer(i, 1);
    }

    /** Copy near last buffer's end */
    copy_buffer(n_buffers - 1, -1);
}

void test_consume()
{
    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    GWBUF* buffer = gwbuf_append(gwbuf_alloc_and_load(5, data),
                                 gwbuf_alloc_and_load(5, data + 5));

    ss_info_dassert(gwbuf_consume(buffer, 0) == buffer,
                    "Consuming 0 bytes from a buffer should return original buffer");
    ss_info_dassert(gwbuf_length(buffer) == 10, "Buffer should be 10 bytes after consuming 0 bytes");

    buffer = gwbuf_consume(buffer, 1);
    ss_info_dassert(GWBUF_LENGTH(buffer) == 4, "First buffer should be 4 bytes long");
    ss_info_dassert(buffer->next, "Buffer should have next pointer set");
    ss_info_dassert(GWBUF_LENGTH(buffer->next) == 5, "Next buffer should be 5 bytes long");
    ss_info_dassert(gwbuf_length(buffer) == 9, "Buffer should be 9 bytes after consuming 1 bytes");
    ss_info_dassert(*((uint8_t*)buffer->start) == 2, "First byte should be 2");

    buffer = gwbuf_consume(buffer, 5);
    ss_info_dassert(buffer->next == NULL, "Buffer should not have the next pointer set");
    ss_info_dassert(GWBUF_LENGTH(buffer) == 4, "Buffer should be 4 bytes after consuming 6 bytes");
    ss_info_dassert(gwbuf_length(buffer) == 4, "Buffer should be 4 bytes after consuming 6 bytes");
    ss_info_dassert(*((uint8_t*)buffer->start) == 7, "First byte should be 7");
    ss_info_dassert(gwbuf_consume(buffer, 4) == NULL, "Consuming all bytes should return NULL");

    buffer = gwbuf_append(gwbuf_alloc_and_load(5, data),
                          gwbuf_alloc_and_load(5, data + 5));
    ss_info_dassert(gwbuf_consume(buffer, 100) == NULL,
                    "Consuming more bytes than are available should return NULL");


    /** Consuming near buffer boudaries */
    for (int i = 0; i < n_buffers - 1; i++)
    {
        consume_buffer(i, -1);
        consume_buffer(i, 0);
        consume_buffer(i, 1);
    }

    /** Consume near last buffer's end */
    consume_buffer(n_buffers - 1, -1);
}

void test_compare()
{
    static const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    ss_dfprintf(stderr, "testbuffer : testing GWBUF comparisons\n");

    GWBUF* lhs = NULL;
    GWBUF* rhs = NULL;

    // Both NULL
    ss_dassert(gwbuf_compare(lhs, rhs) == 0);

    // Either (but not both) NULL
    lhs = gwbuf_alloc_and_load(10, data);
    ss_dassert(gwbuf_compare(lhs, rhs) > 0);
    ss_dassert(gwbuf_compare(rhs, lhs) < 0);

    // The same array
    ss_dassert(gwbuf_compare(lhs, lhs) == 0);

    // Identical array
    gwbuf_free(rhs);
    rhs = gwbuf_alloc_and_load(10, data);
    ss_dassert(gwbuf_compare(lhs, rhs) == 0);

    // One shorter
    gwbuf_free(rhs);
    rhs = gwbuf_alloc_and_load(9, data + 1);
    ss_dassert(gwbuf_compare(lhs, rhs) > 0);
    ss_dassert(gwbuf_compare(rhs, lhs) < 0);

    // One segmented, but otherwise identical.
    gwbuf_free(rhs);
    rhs = NULL;
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(3, data));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(3, data + 3));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(4, data + 3 + 3));

    ss_dassert(gwbuf_compare(lhs, rhs) == 0);
    ss_dassert(gwbuf_compare(rhs, rhs) == 0);

    // Both segmented, but otherwise identical.
    gwbuf_free(lhs);
    lhs = NULL;
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data));
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data + 5));

    ss_dassert(gwbuf_compare(lhs, rhs) == 0);
    ss_dassert(gwbuf_compare(rhs, lhs) == 0);

    // Both segmented and of same length, but different.
    gwbuf_free(lhs);
    lhs = NULL;
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data + 5)); // Values in different order
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data));

    ss_dassert(gwbuf_compare(lhs, rhs) > 0); // 5 > 1
    ss_dassert(gwbuf_compare(rhs, lhs) < 0); // 5 > 1

    // Identical, but one containing empty segments.
    gwbuf_free(rhs);
    rhs = NULL;
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(0, data));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(5, data + 5));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(0, data));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(5, data));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(0, data));

    ss_dassert(gwbuf_compare(lhs, rhs) == 0);
    ss_dassert(gwbuf_compare(rhs, lhs) == 0);

    gwbuf_free(lhs);
    gwbuf_free(rhs);
}



void test_clone()
{
    GWBUF* original = gwbuf_alloc_and_load(1, "1");

    original = gwbuf_append(original, gwbuf_alloc_and_load(1,  "1"));
    original = gwbuf_append(original, gwbuf_alloc_and_load(2,  "12"));
    original = gwbuf_append(original, gwbuf_alloc_and_load(3,  "123"));
    original = gwbuf_append(original, gwbuf_alloc_and_load(5,  "12345"));
    original = gwbuf_append(original, gwbuf_alloc_and_load(8,  "12345678"));
    original = gwbuf_append(original, gwbuf_alloc_and_load(13, "1234567890123"));
    original = gwbuf_append(original, gwbuf_alloc_and_load(21, "123456789012345678901"));

    GWBUF* clone = gwbuf_clone(original);

    GWBUF* o = original;
    GWBUF* c = clone;

    ss_dassert(gwbuf_length(o) == gwbuf_length(c));

    while (o)
    {
        ss_dassert(c);
        ss_dassert(GWBUF_LENGTH(o) == GWBUF_LENGTH(c));

        const char* i = (char*)GWBUF_DATA(o);
        const char* end = i + GWBUF_LENGTH(o);
        const char* j = (char*)GWBUF_DATA(c);

        while (i != end)
        {
            ss_dassert(*i == *j);
            ++i;
            ++j;
        }

        o = o->next;
        c = c->next;
    }

    ss_dassert(c == NULL);

    gwbuf_free(clone);
    gwbuf_free(original);
}

/**
 * test1    Allocate a buffer and do lots of things
 *
 */
static int
test1()
{
    GWBUF   *buffer, *extra, *clone, *partclone;
    HINT    *hint;
    size_t     size = 100;
    size_t     bite1 = 35;
    size_t     bite2 = 60;
    size_t     bite3 = 10;
    size_t     buflen;

    /* Single buffer tests */
    ss_dfprintf(stderr,
                "testbuffer : creating buffer with data size %lu bytes",
                size);
    buffer = gwbuf_alloc(size);
    ss_dfprintf(stderr, "\t..done\nAllocated buffer of size %lu.", size);
    buflen = GWBUF_LENGTH(buffer);
    ss_dfprintf(stderr, "\nBuffer length is now %lu", buflen);
    ss_info_dassert(size == buflen, "Incorrect buffer size");
    ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
    ss_info_dassert(GWBUF_IS_TYPE_UNDEFINED(buffer), "Buffer type should be undefined");
    ss_dfprintf(stderr, "\t..done\nSet a property for the buffer");
    gwbuf_add_property(buffer, (char*)"name", (char*)"value");
    ss_info_dassert(0 == strcmp("value", gwbuf_get_property(buffer, (char*)"name")),
                    "Should now have correct property");
    strcpy((char*)GWBUF_DATA(buffer), "The quick brown fox jumps over the lazy dog");
    ss_dfprintf(stderr, "\t..done\nLoad some data into the buffer");
    ss_info_dassert('q' == GWBUF_DATA_CHAR(buffer, 4), "Fourth character of buffer must be 'q'");
    ss_info_dassert(-1 == GWBUF_DATA_CHAR(buffer, 105), "Hundred and fifth character of buffer must return -1");
    ss_info_dassert(0 == GWBUF_IS_SQL(buffer), "Must say buffer is not SQL, as it does not have marker");
    strcpy((char*)GWBUF_DATA(buffer), "1234\x03SELECT * FROM sometable");
    ss_dfprintf(stderr, "\t..done\nLoad SQL data into the buffer");
    ss_info_dassert(1 == GWBUF_IS_SQL(buffer), "Must say buffer is SQL, as it does have marker");
    clone = gwbuf_clone(buffer);
    ss_dfprintf(stderr, "\t..done\nCloned buffer");
    buflen = GWBUF_LENGTH(clone);
    ss_dfprintf(stderr, "\nCloned buffer length is now %lu", buflen);
    ss_info_dassert(size == buflen, "Incorrect buffer size");
    ss_info_dassert(0 == GWBUF_EMPTY(clone), "Cloned buffer should not be empty");
    ss_dfprintf(stderr, "\t..done\n");
    gwbuf_free(clone);
    ss_dfprintf(stderr, "Freed cloned buffer");
    ss_dfprintf(stderr, "\t..done\n");
    buffer = gwbuf_consume(buffer, bite1);
    ss_info_dassert(NULL != buffer, "Buffer should not be null");
    buflen = GWBUF_LENGTH(buffer);
    ss_dfprintf(stderr, "Consumed %lu bytes, now have %lu, should have %lu", bite1, buflen, size - bite1);
    ss_info_dassert((size - bite1) == buflen, "Incorrect buffer size");
    ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
    ss_dfprintf(stderr, "\t..done\n");
    buffer = gwbuf_consume(buffer, bite2);
    ss_info_dassert(NULL != buffer, "Buffer should not be null");
    buflen = GWBUF_LENGTH(buffer);
    ss_dfprintf(stderr, "Consumed %lu bytes, now have %lu, should have %lu", bite2, buflen, size - bite1 - bite2);
    ss_info_dassert((size - bite1 - bite2) == buflen, "Incorrect buffer size");
    ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
    ss_dfprintf(stderr, "\t..done\n");
    buffer = gwbuf_consume(buffer, bite3);
    ss_dfprintf(stderr, "Consumed %lu bytes, should have null buffer", bite3);
    ss_info_dassert(NULL == buffer, "Buffer should be null");

    /* Buffer list tests */
    size = 100000;
    buffer = gwbuf_alloc(size);
    ss_dfprintf(stderr, "\t..done\nAllocated buffer of size %lu.", size);
    buflen = GWBUF_LENGTH(buffer);
    ss_dfprintf(stderr, "\nBuffer length is now %lu", buflen);
    ss_info_dassert(size == buflen, "Incorrect buffer size");
    ss_info_dassert(0 == GWBUF_EMPTY(buffer), "Buffer should not be empty");
    ss_info_dassert(GWBUF_IS_TYPE_UNDEFINED(buffer), "Buffer type should be undefined");
    extra = gwbuf_alloc(size);
    buflen = GWBUF_LENGTH(buffer);
    ss_dfprintf(stderr, "\t..done\nAllocated extra buffer of size %lu.", size);
    ss_info_dassert(size == buflen, "Incorrect buffer size");
    buffer = gwbuf_append(buffer, extra);
    buflen = gwbuf_length(buffer);
    ss_dfprintf(stderr, "\t..done\nAppended extra buffer to original buffer to create list of size %lu", buflen);
    ss_info_dassert((size * 2) == gwbuf_length(buffer), "Incorrect size for set of buffers");
    buffer = gwbuf_rtrim(buffer, 60000);
    buflen = GWBUF_LENGTH(buffer);
    ss_dfprintf(stderr, "\t..done\nTrimmed 60 bytes from buffer, now size is %lu.", buflen);
    ss_info_dassert((size - 60000) == buflen, "Incorrect buffer size");
    buffer = gwbuf_rtrim(buffer, 60000);
    buflen = GWBUF_LENGTH(buffer);
    ss_dfprintf(stderr, "\t..done\nTrimmed another 60 bytes from buffer, now size is %lu.", buflen);
    ss_info_dassert(100000 == buflen, "Incorrect buffer size");
    ss_info_dassert(buffer == extra, "The buffer pointer should now point to the extra buffer");
    ss_dfprintf(stderr, "\t..done\n");
    gwbuf_free(buffer);
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
    GWBUF* all_clones = gwbuf_clone(head);
    ss_info_dassert(all_clones && all_clones->next, "Cloning all should work");
    ss_info_dassert(GWBUF_LENGTH(all_clones) == headsize, "First buffer should be 10 bytes");
    ss_info_dassert(GWBUF_LENGTH(all_clones->next) == tailsize, "Second buffer should be 20 bytes");
    ss_info_dassert(gwbuf_length(all_clones) == headsize + tailsize, "Total buffer length should be 30 bytes");
    gwbuf_free(all_clones);
    gwbuf_free(head);

    test_split();
    test_load_and_copy();
    test_consume();
    test_compare();
    test_clone();

    return 0;
}

int main(int argc, char **argv)
{
    int result = 0;

    result += test1();

    exit(result);
}


