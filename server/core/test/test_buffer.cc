/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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

void test_split()
{
    printf("Testing splitting\n");
    size_t headsize = 10;
    size_t tailsize = 20;

    GWBUF head(headsize);
    GWBUF tail(tailsize);
    head.append(tail);
    TEST(head.length() == headsize + tailsize);
    GWBUF newchain = head.split(headsize + 5);
    TEST(newchain.length() == headsize + 5);
    TEST(head.length() == tailsize - 5);

    /** Bad parameter tests */
    GWBUF buffer(headsize);
    auto splitted = buffer.split(0);
    TEST(splitted.length() == 0);
    TEST(buffer.length() == headsize);
}

void test_load_and_copy()
{
    printf("Testing copying from buffer\n");
    const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dest[8];
    GWBUF head(data, 4);
    GWBUF tail(data + 4, 4);

    TEST(memcmp(head.data(), data, 4) == 0);
    TEST(memcmp(tail.data(), data + 4, 4) == 0);

    memset(dest, 0, sizeof(dest));
    TEST(head.copy_data(0, 4, dest) == 4);
    test(memcmp(dest, data, 4) == 0, "Copied data should be from 1 to 4");

    memset(dest, 0, sizeof(dest));
    head.append(tail);
    TEST(head.copy_data(0, 8, dest) == 8);
    test(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    memset(dest, 0, sizeof(dest));
    TEST(head.copy_data(4, 4, dest) == 4);
    test(memcmp(dest, data + 4, 4) == 0, "Copied data should be from 5 to 8");

    memset(dest, 0, sizeof(dest));
    test(head.copy_data(0, 10, dest) == 8, "Copying 10 bytes should only copy 8 bytes");
    test(memcmp(dest, data, 8) == 0, "Copied data should be from 1 to 8");

    memset(dest, 0, sizeof(dest));
    test(head.copy_data(0, 0, dest) == 0, "Copying 0 bytes should not copy any bytes");
}

void test_consume()
{
    printf("Testing consume and indexing\n");
    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    GWBUF buffer(data, 10);
    TEST(buffer.length() == 10);

    buffer.consume(1);
    TEST(buffer.length() == 9);
    TEST(buffer[0] == 2);

    buffer.consume(5);
    TEST(buffer.length() == 4);
    TEST(buffer[0] == 7);
    buffer.consume(4);
    TEST(buffer.empty());
}

void test_compare()
{
    static const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    printf("Testing comparison\n");
    GWBUF lhs(data, 10);

    // The same array
    TEST(lhs.compare(lhs) == 0);

    // Identical array
    GWBUF rhs(data, 10);
    TEST(lhs.compare(rhs) == 0);

    // One shorter
    rhs = GWBUF(data + 1, 9);
    TEST(lhs.compare(rhs) > 0);
    TEST(rhs.compare(lhs) < 0);

    // Built in parts, but otherwise identical.
    rhs.clear();
    rhs.append(data, 3);
    rhs.append(data + 3, 3);
    rhs.append(data + 6, 4);

    TEST(lhs.compare(rhs) == 0);
    TEST(rhs.compare(rhs) == 0);

    // Both segmented and of same length, but different.
    lhs.clear();
    lhs.append(data + 5, 5);    // Values in different order
    lhs.append(data, 5);

    TEST(lhs.compare(rhs) > 0);     // 5 > 1
    TEST(rhs.compare(lhs) < 0);     // 5 > 1
}


void test_basics()
{
    printf("Testing basics\n");
    size_t size = 100;
    GWBUF buffer(size);
    TEST(!buffer.empty());
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
    TEST(!buffer.empty());
    TEST(buffer.length() == size);
    TEST(buffer.type_is_undefined());
    GWBUF extra(size);
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
    GWBUF shallow_clone = orig.shallow_clone();
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
