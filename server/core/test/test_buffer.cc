/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// To ensure that ss_info_assert asserts also when building in non-debug mode.
#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#if defined (NDEBUG)
#undef NDEBUG
#endif
#include <cstdio>
#include <maxbase/log.hh>
#include <maxbase/alloc.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

namespace
{
int fails = 0;
void test(bool result, const char* msg)
{
    if (!result)
    {
        fails++;
        printf("%s\n", msg);
    }
}

void test(bool result, int line)
{
    if (!result)
    {
        fails++;
        printf("Test failure on line %i\n", line);
    }
}

#define TEST(t) test(t, __LINE__)

/**
 * Generate predefined test data
 *
 * @param count Number of bytes to generate
 * @return Pointer to @c count bytes of data
 */
uint8_t* generate_data(size_t count)
{
    uint8_t* data = (uint8_t*)MXB_MALLOC(count);
    MXB_ABORT_IF_NULL(data);

    srand(0);

    for (size_t i = 0; i < count; i++)
    {
        data[i] = rand() % 256;
    }

    return data;
}

size_t buffers[] =
{
    2,  3,  5,  7,  11, 13, 17,  19,  23,  29,  31,  37,  41,  43,  47,  53, 59, 61, 67,
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

    MXB_FREE(data);

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

    mxb_assert_message(buffer && newbuf, "Both buffers should be non-NULL");
    mxb_assert_message(gwbuf_length(newbuf) == cutoff, "New buffer should be have correct length");
    mxb_assert_message(gwbuf_length(buffer) == len - cutoff, "Old buffer should be have correct length");
    gwbuf_free(buffer);
    gwbuf_free(newbuf);
}


void consume_buffer(int n, int offset)
{
    size_t cutoff = get_length_at(n) + offset;
    GWBUF* buffer = create_test_buffer();
    int len = gwbuf_length(buffer);
    buffer = gwbuf_consume(buffer, cutoff);

    mxb_assert_message(buffer, "Buffer should be non-NULL");
    mxb_assert_message(gwbuf_length(buffer) == len - cutoff, "Buffer should be have correct length");
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
    mxb_assert_message(gwbuf_copy_data(buffer, 0, cutoff, dest) == cutoff, "All bytes should be read");
    mxb_assert_message(memcmp(data, dest, sizeof(dest)) == 0, "Data should be OK");
    gwbuf_free(buffer);
    MXB_FREE(data);
}

/** gwbuf_split test - These tests assume allocation will always succeed */
void test_split()
{
    size_t headsize = 10;
    size_t tailsize = 20;

    GWBUF head(headsize);
    head.write_complete(headsize);
    GWBUF tail(tailsize);
    tail.write_complete(tailsize);
    head.append(tail);
    TEST(head.length() == headsize + tailsize);
    GWBUF newchain = head.split(headsize + 5);
    TEST(newchain.length() == headsize + 5);
    TEST(head.length() == tailsize - 5);

    /** Bad parameter tests */
    GWBUF buffer(headsize);
    buffer.write_complete(headsize);
    auto splitted = buffer.split(0);
    TEST(splitted.length() == 0);
    TEST(buffer.length() == headsize);

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

    mxb_assert_message(memcmp(GWBUF_DATA(head), data, 4) == 0, "Loading 4 bytes should succeed");
    mxb_assert_message(memcmp(GWBUF_DATA(tail), data + 4, 4) == 0, "Loading 4 bytes should succeed");

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(head, 0, 4, dest) == 4, "Copying 4 bytes should succeed");
    mxb_assert_message(memcmp(dest, data, 4) == 0, "Copied data should be from 1 to 4");

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(tail, 0, 4, dest) == 4, "Copying 4 bytes should succeed");
    mxb_assert_message(memcmp(dest, data + 4, 4) == 0, "Copied data should be from 5 to 8");
    head = gwbuf_append(head, tail);

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(head, 0, 8, dest) == 8, "Copying 8 bytes should succeed");
    mxb_assert_message(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(head, 4, 4, dest) == 4, "Copying 4 bytes at offset 4 should succeed");
    mxb_assert_message(memcmp(dest, data + 4, 4) == 0, "Copied data should be from 5 to 8");

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(head, 2, 4, dest) == 4, "Copying 4 bytes at offset 2 should succeed");
    mxb_assert_message(memcmp(dest, data + 2, 4) == 0, "Copied data should be from 5 to 8");

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(head, 0, 10, dest) == 8, "Copying 10 bytes should only copy 8 bytes");
    mxb_assert_message(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(head, 0, 0, dest) == 0, "Copying 0 bytes should not copy any bytes");

    memset(dest, 0, sizeof(dest));
    mxb_assert_message(gwbuf_copy_data(head, 0, -1, dest) == sizeof(data),
                       "Copying -1 bytes should copy all available data (cast to unsigned)");
    mxb_assert_message(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    mxb_assert_message(gwbuf_copy_data(head, -1, -1, dest) == 0,
                       "Copying -1 bytes at an offset of -1 should not copy any bytes");
    mxb_assert_message(gwbuf_copy_data(head, -1, 0, dest) == 0,
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

    mxb_assert_message(gwbuf_length(buffer) == 10, "Buffer should be 10 bytes after consuming 0 bytes");

    buffer = gwbuf_consume(buffer, 1);
    mxb_assert_message(gwbuf_length(buffer) == 9, "Buffer should be 9 bytes after consuming 1 bytes");
    mxb_assert_message(*(buffer->data()) == 2, "First byte should be 2");

    buffer = gwbuf_consume(buffer, 5);
    mxb_assert_message(gwbuf_length(buffer) == 4, "Buffer should be 4 bytes after consuming 6 bytes");
    mxb_assert_message(*(buffer->data()) == 7, "First byte should be 7");
    mxb_assert_message(gwbuf_consume(buffer, 4) == NULL, "Consuming all bytes should return NULL");

    buffer = gwbuf_append(gwbuf_alloc_and_load(5, data),
                          gwbuf_alloc_and_load(5, data + 5));
    mxb_assert_message(gwbuf_consume(buffer, 10) == NULL,
                       "Consuming all bytes should return NULL");


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

    fprintf(stderr, "testbuffer : testing GWBUF comparisons\n");

    GWBUF* lhs = gwbuf_alloc_and_load(10, data);

    // The same array
    mxb_assert(gwbuf_compare(lhs, lhs) == 0);

    // Identical array
    GWBUF* rhs = gwbuf_alloc_and_load(10, data);
    mxb_assert(gwbuf_compare(lhs, rhs) == 0);

    // One shorter
    gwbuf_free(rhs);
    rhs = gwbuf_alloc_and_load(9, data + 1);
    mxb_assert(gwbuf_compare(lhs, rhs) > 0);
    mxb_assert(gwbuf_compare(rhs, lhs) < 0);

    // One segmented, but otherwise identical.
    gwbuf_free(rhs);
    rhs = NULL;
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(3, data));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(3, data + 3));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(4, data + 3 + 3));

    mxb_assert(gwbuf_compare(lhs, rhs) == 0);
    mxb_assert(gwbuf_compare(rhs, rhs) == 0);

    // Both segmented, but otherwise identical.
    gwbuf_free(lhs);
    lhs = NULL;
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data));
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data + 5));

    mxb_assert(gwbuf_compare(lhs, rhs) == 0);
    mxb_assert(gwbuf_compare(rhs, lhs) == 0);

    // Both segmented and of same length, but different.
    gwbuf_free(lhs);
    lhs = NULL;
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data + 5));     // Values in different order
    lhs = gwbuf_append(lhs, gwbuf_alloc_and_load(5, data));

    mxb_assert(gwbuf_compare(lhs, rhs) > 0);    // 5 > 1
    mxb_assert(gwbuf_compare(rhs, lhs) < 0);    // 5 > 1

    // Identical, but one containing empty segments.
    gwbuf_free(rhs);
    rhs = NULL;
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(5, data + 5));
    rhs = gwbuf_append(rhs, gwbuf_alloc_and_load(5, data));

    mxb_assert(gwbuf_compare(lhs, rhs) == 0);
    mxb_assert(gwbuf_compare(rhs, lhs) == 0);

    gwbuf_free(lhs);
    gwbuf_free(rhs);
}


void test_basics()
{
    printf("Testing basics\n");
    size_t size = 100;
    GWBUF buffer(size);
    TEST(buffer.empty());
    buffer.write_complete(size);
    TEST(buffer.length() == size);
    TEST(!buffer.empty());
    TEST(buffer.type_is_undefined());
    strcpy(reinterpret_cast<char*>(buffer.data()), "The quick brown fox jumps over the lazy dog");
    test(buffer[4] == 'q', "Fourth character of buffer must be 'q'");
    test(!mariadb::is_com_query(buffer), "Buffer should not be SQL");
    strcpy(reinterpret_cast<char*>(buffer.data()), "1234\x03SELECT * FROM sometable");
    test(mariadb::is_com_query(buffer), "Buffer should be SQL");

    printf("Testing consume\n");
    size_t bite1 = 35;
    buffer.consume(bite1);
    TEST(buffer.length() == size - bite1);
    TEST(!buffer.empty());
    size_t bite2 = 60;
    buffer.consume(bite2);
    TEST(buffer.length() == (size - bite1 - bite2));
    size_t bite3 = 5;
    buffer.consume(bite3);
    TEST(buffer.empty());

    printf("Testing append and trim\n");
    size = 100000;
    buffer = GWBUF(size);
    TEST(buffer.empty());
    buffer.write_complete(size);
    TEST(buffer.length() == size);
    TEST(buffer.type_is_undefined());
    GWBUF extra(size);
    extra.write_complete(size);
    buffer.append(extra);
    test(buffer.length() == 2 * size, "Incorrect size for extended buffer");
    buffer.rtrim(60000);
    test(buffer.length() == (2 * size - 60000), "Incorrect buffer size after trimming");
    buffer.rtrim(60000);
    test(buffer.length() == 80000, "Incorrect buffer size after another trim");

    printf("Testing cloning\n");
    const uint8_t message[] = "12345";
    auto len = sizeof(message);
    GWBUF orig(message, len);
    GWBUF shallow_clone(orig);
    GWBUF deep_clone = orig.deep_clone();
    test(orig.length() == len && shallow_clone.length() == len && deep_clone.length() == len,
         "Wrong length after cloning");
    TEST(memcmp(orig.data(), message, len) == 0);
    TEST(memcmp(shallow_clone.data(), message, len) == 0);
    TEST(memcmp(deep_clone.data(), message, len) == 0);

    orig.data()[3] = 'X';
    const uint8_t message2[] = "123X5";
    TEST(memcmp(orig.data(), message2, len) == 0);
    TEST(memcmp(shallow_clone.data(), message2, len) == 0);
    TEST(memcmp(deep_clone.data(), message, len) == 0);

    // Append to original, should become unique.
    orig.append(message, len);
    orig.data()[3] = 'Y';
    const uint8_t message3[] = "123Y5\0""12345";
    TEST(orig.length() == 2 * len);
    TEST(memcmp(orig.data(), message3, 2 * len) == 0);
    TEST(memcmp(shallow_clone.data(), message2, len) == 0);
}
}

int main(int argc, char** argv)
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);
    test_basics();
    test_split();
    test_load_and_copy();
    test_consume();
    test_compare();
    return fails;
}
