/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdio.h>
#include <string.h>
#include <maxbase/format.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

namespace
{
void expect_impl(int linenum, bool res, const char* fmt, ...) __attribute__ ((format(printf, 3, 4)));

#define expect(format, ...) expect_impl(__LINE__, format, ##__VA_ARGS__)

int retval = 0;

void expect_impl(int linenum, bool res, const char* fmt, ...)
{
    if (!res)
    {
        va_list valist;
        va_start(valist, fmt);
        std::string msg = mxb::string_vprintf(fmt, valist);
        va_end(valist);
        retval++;
        printf("ERROR on line %i: %s\n", linenum, msg.c_str());
    }
}

void test1()
{
    const int writelen = 100;
    GWBUF buffer(writelen);
    memset(buffer.data(), 0, writelen);
    expect(buffer.length() == writelen, "Length should be correct");
    expect(mariadb::is_com_query(buffer) == false, "Default buffer should not be diagnosed as SQL");
    expect(mariadb::get_sql(buffer).empty(), "Default buffer should fail");
}

void test2()
{
    unsigned int len = 128;
    /** Allocate space for the COM_QUERY header and payload */
    auto total_len = MYSQL_HEADER_LEN + 1 + len;
    GWBUF buffer(total_len);

    char query[len + 1];
    memset(query, ';', len);
    memset(query + len, '\0', 1);
    auto ptr = buffer.data();
    ptr = mariadb::write_header(ptr, len, 1);
    *ptr++ = 0x03;
    mariadb::copy_chars(ptr, query, strlen(query));

    std::string_view sv = mariadb::get_sql(buffer);
    const char* sql = sv.data();
    int length = sv.length();
    expect(strncmp(sql, query, len) == 0, "SQL should match");
}

/** This is a standard OK packet */
const uint8_t ok[] =
{
    0x07, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00
};

/** Created with:
 * CREATE OR REPLACE TABLE test.t1 (id int);
 * INSERT INTO test.t1 VALUES (3000);
 * SELECT * FROM test.t1; */
const uint8_t resultset[] =
{
    /* Packet 1 */
    0x01, 0x00, 0x00, 0x01, 0x01,
    /* Packet 2 */
    0x22, 0x00, 0x00, 0x02, 0x03,0x64,  0x65, 0x66, 0x04, 0x74, 0x65, 0x73, 0x74, 0x02, 0x74, 0x31,
    0x02, 0x74, 0x31, 0x02, 0x69,0x64,  0x02, 0x69, 0x64, 0x0c, 0x3f,
    0x00, 0x0b, 0x00, 0x00, 0x00,0x03,  0x00, 0x00, 0x00, 0x00, 0x00,
    /* Packet 3 */
    0x05, 0x00, 0x00, 0x03, 0xfe,0x00,  0x00, 0x22, 0x00,
    /* Packet 4 */
    0x05, 0x00, 0x00, 0x04, 0x04,0x33,  0x30, 0x30, 0x30,
    /* Packet 5 */
    0x05, 0x00, 0x00, 0x05, 0xfe,0x00,  0x00, 0x22, 0x00
};
}

#define PACKET_HDR_LEN 4

#define PACKET_1_IDX 0
#define PACKET_1_LEN (PACKET_HDR_LEN + 0x01)        // resultset[PACKET_1_IDX])
#define PACKET_2_IDX (PACKET_1_IDX + PACKET_1_LEN)
#define PACKET_2_LEN (PACKET_HDR_LEN + 0x22)        // resultset[PACKET_2_IDX]);
#define PACKET_3_IDX (PACKET_2_IDX + PACKET_2_LEN)
#define PACKET_3_LEN (PACKET_HDR_LEN + 0x05)        // resultset[PACKET_3_IDX]);
#define PACKET_4_IDX (PACKET_3_IDX + PACKET_3_LEN)
#define PACKET_4_LEN (PACKET_HDR_LEN + 0x05)        // resultset[PACKET_4_IDX]);
#define PACKET_5_IDX (PACKET_4_IDX + PACKET_4_LEN)
#define PACKET_5_LEN (PACKET_HDR_LEN + 0x05)        // resultset[PACKET_5_IDX]);

struct packet
{
    int          index;
    unsigned int length;
} packets[] =
{
    {PACKET_1_IDX, PACKET_1_LEN},
    {PACKET_2_IDX, PACKET_2_LEN},
    {PACKET_3_IDX, PACKET_3_LEN},
    {PACKET_4_IDX, PACKET_4_LEN},
    {PACKET_5_IDX, PACKET_5_LEN},
};

#define N_PACKETS (sizeof(packets) / sizeof(packets[0]))


//
// modutil_get_complete_packets
//
void test_single_sql_packet1()
{
    /** Single packet */
    GWBUF buffer(ok, sizeof(ok));
    GWBUF complete = mariadb::get_complete_packets(buffer);
    expect(buffer.empty(), "Old buffer should be empty");
    expect(!complete.empty(), "Complete packet buffer should not be empty");
    expect(complete.length() == sizeof(ok), "Complete packet buffer should contain enough data");
    expect(memcmp(complete.data(), ok, complete.length()) == 0,
           "Complete packet buffer's data should be equal to original data");

    /** Partial single packet */
    buffer = GWBUF(ok, sizeof(ok) - 4);
    complete = mariadb::get_complete_packets(buffer);
    expect(!buffer.empty(), "Old buffer should be not empty");
    expect(complete.empty(), "Complete packet buffer should be empty");
    expect(buffer.length() == sizeof(ok) - 4, "Old buffer should contain right amount of data");

    /** Add the missing data */
    buffer.append(ok + sizeof(ok) - 4, 4);
    complete = mariadb::get_complete_packets(buffer);
    expect(buffer.empty(), "Old buffer should be empty");
    expect(!complete.empty(), "Complete packet buffer should not be empty");
    expect(complete.length() == sizeof(ok), "Buffer should contain all data");
}

void test_multiple_sql_packets1()
{
    /** All of the data */
    GWBUF buffer(resultset, sizeof(resultset));
    GWBUF complete = mariadb::get_complete_packets(buffer);
    expect(buffer.empty(), "Old buffer should be empty");
    expect(!complete.empty(), "Complete packet buffer should not be NULL");
    expect(complete.length() == sizeof(resultset),
           "Complete packet buffer should contain enough data");
    expect(memcmp(complete.data(), resultset, complete.length()) == 0,
           "Complete packet buffer's data should be equal to original data");

    /** Partial data available with one complete packet */
    GWBUF head(resultset, 7);
    GWBUF tail(resultset + 7, sizeof(resultset) - 7);
    complete = mariadb::get_complete_packets(head);
    expect(!head.empty(), "Old buffer should not be empty");
    expect(!complete.empty(), "Complete buffer should not be empty");
    expect(complete.length() == 5, "Complete buffer should contain first packet only");
    expect(head.length() == 2, "Complete buffer should contain first packet only");

    /** All packets are available */
    head.append(tail);
    complete = mariadb::get_complete_packets(head);
    expect(head.empty(), "Old buffer should be empty");
    expect(!complete.empty(), "Complete packet buffer should not be empty");
    expect(complete.length() == sizeof(resultset) - 5,
           "Complete packet should be sizeof(resultset) - 5 bytes");

    /** Sliding cutoff of the buffer boundary */
    for (size_t i = 1; i < sizeof(resultset); i++)
    {
        head = GWBUF(resultset, i);
        tail = GWBUF(resultset + i, sizeof(resultset) - i);
        head.append(tail);
        complete = mariadb::get_complete_packets(head);
        auto headlen = head.length();
        auto completelen = complete.length();
        expect(headlen + completelen == sizeof(resultset),
               "Both buffers should sum up to sizeof(resultset) bytes");
        uint8_t databuf[sizeof(resultset)];
        complete.copy_data(0, completelen, databuf);
        if (!head.empty())
        {
            head.copy_data(0, headlen, databuf + completelen);
        }
        expect(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");
    }

    /** Fragmented buffer chain */
    size_t chunk = 5;
    size_t total = 0;
    head.clear();

    do
    {
        chunk = chunk + 5 < sizeof(resultset) ? 5 : (chunk + 5) - sizeof(resultset);
        head.append(resultset + total, chunk);
        total += chunk;
    }
    while (total < sizeof(resultset));

    expect(head.length() == sizeof(resultset), "Head should be sizeof(resultset) bytes long");
    complete = mariadb::get_complete_packets(head);
    expect(head.empty(), "Head should be empty");
    expect(!complete.empty(), "Complete should not be empty");
    expect(complete.length() == sizeof(resultset),
           "Complete should be sizeof(resultset) bytes long");

    auto headlen = head.length();
    auto completelen = complete.length();
    uint8_t databuf[sizeof(resultset)];
    expect(complete.copy_data(0, completelen, databuf) == completelen,
           "Expected data should be readable");
    if (!head.empty())
    {
        expect(head.copy_data(0, headlen, databuf + completelen) == headlen,
               "Expected data should be readable");
    }
    expect(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");

    /** Fragmented buffer split into multiple chains and then reassembled as a complete resultset */
    GWBUF half = complete.shallow_clone();
    GWBUF quarter = half.split(half.length() / 2);
    head = quarter.split(quarter.length() / 2);
    expect(!half.empty() && !quarter.empty() && !head.empty(), "split should work");

    complete = mariadb::get_complete_packets(head);
    expect(!complete.empty() && !head.empty(), "Both buffers should have data");
    expect(complete.length() + head.length() + quarter.length()
           + half.length() == sizeof(resultset), "A quarter of data should be available");

    complete.append(head);
    complete.append(quarter);
    quarter = std::move(complete);
    complete = mariadb::get_complete_packets(quarter);
    expect(complete.length() + quarter.length() + half.length() == sizeof(resultset),
           "half of data should be available");

    complete.append(quarter);
    complete.append(half);
    half = std::move(complete);
    complete = mariadb::get_complete_packets(half);
    expect(!complete.empty(), "Complete should not be empty");
    expect(half.empty(), "Old buffer should be empty");
    expect(complete.length() == sizeof(resultset), "Complete should contain all of the data");

    completelen = complete.length();
    expect(complete.copy_data(0, completelen, databuf) == completelen,
           "All data should be readable");
    expect(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");
}

//
// modutil_get_next_MySQL_packet
//
void test_single_sql_packet2()
{
    /** Single packet */
    GWBUF buffer(ok, sizeof(ok));
    GWBUF next = mariadb::get_next_MySQL_packet(buffer);
    expect(buffer.empty(), "Old buffer should be empty");
    expect(!next.empty(), "Next packet buffer should not be empty");
    expect(next.length() == sizeof(ok), "Next packet buffer should contain enough data");
    expect(memcmp(next.data(), ok, next.length()) == 0,
           "Next packet buffer's data should be equal to original data");

    /** Partial single packet */
    buffer = GWBUF(ok, sizeof(ok) - 4);
    next = mariadb::get_next_MySQL_packet(buffer);
    expect(!buffer.empty(), "Old buffer should be not empty");
    expect(next.empty(), "Next packet buffer should be empty");
    expect(buffer.length() == sizeof(ok) - 4, "Old buffer should contain right amount of data");

    /** Add the missing data */
    buffer.append(ok + sizeof(ok) - 4, 4);
    next = mariadb::get_next_MySQL_packet(buffer);
    expect(buffer.empty(), "Old buffer should be empty");
    expect(!next.empty(), "Next packet buffer should not be empty");
    expect(next.length() == sizeof(ok), "Buffer should contain all data");
}

void test_multiple_sql_packets2()
{
    /** All of the data */
    GWBUF buffer(resultset, sizeof(resultset));

    // Empty buffer packet by packet.
    for (unsigned int i = 0; i < N_PACKETS; i++)
    {
        GWBUF next = mariadb::get_next_MySQL_packet(buffer);
        expect(!next.empty(), "Next packet buffer should not be NULL");
        expect(next.length() == packets[i].length,
               "Next packet buffer should contain enough data");
        expect(memcmp(next.data(), &resultset[packets[i].index], next.length()) == 0,
               "Next packet buffer's data should be equal to original data");
    }
    expect(buffer.empty(), "Buffer should be empty");

    // Exactly one packet
    size_t len = PACKET_1_LEN;
    buffer.append(resultset, len);
    GWBUF next = mariadb::get_next_MySQL_packet(buffer);
    expect(buffer.empty(), "Old buffer should be empty.");
    expect(next.length() == PACKET_1_LEN, "Length should match.");
    next.clear();

    // Slightly less than one packet
    len = PACKET_1_LEN - 1;
    buffer.append(resultset, len);
    next = mariadb::get_next_MySQL_packet(buffer);
    expect(!buffer.empty(), "Old buffer should not be empty.");
    expect(next.empty(), "Next should be empty.");

    GWBUF tail(resultset + len, (sizeof(resultset) - len));
    buffer.append(tail);
    next = mariadb::get_next_MySQL_packet(buffer);
    expect(!buffer.empty(), "Old buffer should not be empty.");
    expect(next.length() == PACKET_1_LEN, "Length should match.");

    // Slightly more than one packet
    len = PACKET_1_LEN + 1;
    buffer = GWBUF(resultset, len);
    next = mariadb::get_next_MySQL_packet(buffer);
    expect(!buffer.empty(), "Old buffer should not be empty.");
    expect(next.length() == PACKET_1_LEN, "Length should match.");

    next = mariadb::get_next_MySQL_packet(buffer);
    expect(!buffer.empty(), "Old buffer should not be empty.");
    expect(next.empty(), "Next should be empty.");

    tail = GWBUF(resultset + len, sizeof(resultset) - len);
    buffer.append(tail);
    next = mariadb::get_next_MySQL_packet(buffer);
    expect(!buffer.empty(), "Old buffer should not be empty.");
    expect(next.length() == PACKET_2_LEN, "Length should match.");
    expect(memcmp(next.data(), &resultset[PACKET_2_IDX], next.length()) == 0,
           "Next packet buffer's data should be equal to original data");

    GWBUF head;
    /** Sliding cutoff of the buffer boundary */
    for (size_t i = 1; i < sizeof(resultset); i++)
    {
        head = GWBUF(resultset, i);
        tail = GWBUF(resultset + i, sizeof(resultset) - i);
        head.append(tail);
        next = mariadb::get_next_MySQL_packet(head);
        auto headlen = head.length();
        auto nextlen = next.length();
        expect(headlen + nextlen == sizeof(resultset),
               "Both buffers should sum up to sizeof(resultset) bytes");
        uint8_t databuf[sizeof(resultset)];
        next.copy_data(0, nextlen, databuf);
        head.copy_data(0, headlen, databuf + nextlen);
        expect(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");
    }

    /** Fragmented buffer chain */
    size_t chunk = 5;
    size_t total = 0;
    buffer.clear();

    do
    {
        chunk = chunk + 5 < sizeof(resultset) ? 5 : (chunk + 5) - sizeof(resultset);
        buffer.append(resultset + total, chunk);
        total += chunk;
    }
    while (total < sizeof(resultset));

    for (unsigned int i = 0; i < N_PACKETS; i++)
    {
        GWBUF next2 = mariadb::get_next_MySQL_packet(buffer);
        expect(next2.length() == packets[i].length,
               "Next packet buffer should contain enough data");
        expect(memcmp(next2.data(), &resultset[packets[i].index], next2.length()) == 0,
               "Next packet buffer's data should be equal to original data");
    }
    expect(buffer.empty(), "Buffer should be empty");
}

void test_strnchr_esc_mariadb()
{
    char comment1[] = "This will -- fail.";
    expect(mxb::strnchr_esc_mariadb(comment1, '.', sizeof(comment1) - 1) == NULL,
           "Commented character should return NULL");

    char comment2[] = "This will # fail.";
    expect(mxb::strnchr_esc_mariadb(comment2, '.', sizeof(comment2) - 1) == NULL,
           "Commented character should return NULL");

    char comment3[] = "This will fail/* . */";
    expect(mxb::strnchr_esc_mariadb(comment3, '.', sizeof(comment3) - 1) == NULL,
           "Commented character should return NULL");

    char comment4[] = "This will not /* . */ fail.";
    expect(mxb::strnchr_esc_mariadb(comment4, '.', sizeof(comment4) - 1) == strrchr(comment4, '.'),
           "Uncommented character should be matched");

    char comment5[] = "This will fail/* . ";
    expect(mxb::strnchr_esc_mariadb(comment5, '.', sizeof(comment5) - 1) == NULL,
           "Bad comment should fail");
}

void test_strnchr_esc()
{
    /** Single escaped and quoted characters */
    char esc1[] = "This will fail\\.";
    expect(mxb::strnchr_esc(esc1, '.', sizeof(esc1) - 1) == NULL,
           "Only escaped character should return NULL");
    expect(mxb::strnchr_esc_mariadb(esc1, '.', sizeof(esc1) - 1) == NULL,
           "Only escaped character should return NULL");

    char esc2[] = "This will fail\".\"";
    expect(mxb::strnchr_esc(esc1, '.', sizeof(esc1) - 1) == NULL,
           "Only escaped character should return NULL");
    expect(mxb::strnchr_esc_mariadb(esc1, '.', sizeof(esc1) - 1) == NULL,
           "Only escaped character should return NULL");

    char esc3[] = "This will fail'.'";
    expect(mxb::strnchr_esc(esc1, '.', sizeof(esc1) - 1) == NULL,
           "Only escaped character should return NULL");
    expect(mxb::strnchr_esc_mariadb(esc1, '.', sizeof(esc1) - 1) == NULL,
           "Only escaped character should return NULL");

    /** Test escaped and quoted characters */
    char str1[] = "this \\. is a test.";
    expect(mxb::strnchr_esc(str1, '.', sizeof(str1) - 1) == strrchr(str1, '.'),
           "Escaped characters should be ignored");
    expect(mxb::strnchr_esc_mariadb(str1, '.', sizeof(str1) - 1) == strrchr(str1, '.'),
           "Escaped characters should be ignored");
    char str2[] = "this \"is . \" a test .";
    expect(mxb::strnchr_esc(str2, '.', sizeof(str2) - 1) == strrchr(str2, '.'),
           "Double quoted characters should be ignored");
    expect(mxb::strnchr_esc_mariadb(str2, '.', sizeof(str2) - 1) == strrchr(str2, '.'),
           "Double quoted characters should be ignored");
    char str3[] = "this 'is . ' a test .";
    expect(mxb::strnchr_esc(str3, '.', sizeof(str3) - 1) == strrchr(str3, '.'),
           "Double quoted characters should be ignored");
    expect(mxb::strnchr_esc_mariadb(str3, '.', sizeof(str3) - 1) == strrchr(str3, '.'),
           "Double quoted characters should be ignored");

    /** Bad quotation tests */
    char bad1[] = "This will \" fail.";
    expect(mxb::strnchr_esc(bad1, '.', sizeof(bad1) - 1) == NULL, "Bad quotation should fail");
    expect(mxb::strnchr_esc_mariadb(bad1, '.', sizeof(bad1) - 1) == NULL, "Bad quotation should fail");

    char bad2[] = "This will ' fail.";
    expect(mxb::strnchr_esc(bad2, '.', sizeof(bad2) - 1) == NULL, "Bad quotation should fail");
    expect(mxb::strnchr_esc_mariadb(bad2, '.', sizeof(bad2) - 1) == NULL, "Bad quotation should fail");

    char bad3[] = "This will \" fail. '";
    expect(mxb::strnchr_esc(bad3, '.', sizeof(bad3) - 1) == NULL, "Different quote pairs should fail");
    expect(mxb::strnchr_esc_mariadb(bad3, '.', sizeof(bad3) - 1) == NULL,
           "Different quote pairs should fail");

    char bad4[] = "This will ' fail. \"";
    expect(mxb::strnchr_esc(bad4, '.', sizeof(bad4) - 1) == NULL, "Different quote pairs should fail");
    expect(mxb::strnchr_esc_mariadb(bad4, '.', sizeof(bad4) - 1) == NULL,
           "Different quote pairs should fail");
}

GWBUF create_buffer(size_t size)
{
    GWBUF buffer(size + MYSQL_HEADER_LEN);
    mariadb::write_header(buffer.data(), size, 0);
    return buffer;
}

void test_large_packets()
{
    /** Two complete large packets */
    for (int i = -4; i < 5; i++)
    {
        unsigned long ul = 0x00ffffff + i;
        size_t first_len = ul > 0x00ffffff ? 0x00ffffff : ul;
        GWBUF buffer = create_buffer(first_len);

        if (first_len < ul)
        {
            buffer.append(create_buffer(ul - first_len));
        }
        size_t before = buffer.length();
        GWBUF complete = mariadb::get_complete_packets(buffer);

        expect(buffer.empty(), "Original buffer should be empty");
        expect(!complete.empty(), "Complete buffer should not be empty");
        expect(complete.length() == before, "Complete buffer should contain all data");
    }

    /** Incomplete packet */
    for (int i = 0; i < 5; i++)
    {
        GWBUF buffer = create_buffer(0x00ffffff - i);
        buffer.rtrim(4);
        GWBUF complete = mariadb::get_complete_packets(buffer);
        expect(!buffer.empty(), "Incomplete buffer should not be empty");
        expect(complete.empty(), "The complete buffer should be empty");
    }

    /** Incomplete second packet */
    for (int i = 2; i < 8; i++)
    {
        GWBUF buffer = create_buffer(0x00ffffff);
        buffer.append(create_buffer(i));
        mxb_assert(buffer.length() == 0xffffffUL + i + 8);
        buffer.rtrim(1);
        GWBUF complete = mariadb::get_complete_packets(buffer);
        expect(!buffer.empty(), "Incomplete buffer should not be empty");
        expect(!complete.empty(), "The complete buffer should not be empty");
        expect(complete.length() == 0xffffff + 4, "Length should be correct");
    }
}

const char* bypass_whitespace(const char* sql)
{
    return mariadb::bypass_whitespace(sql, strlen(sql));
}

void test_bypass_whitespace()
{
    const char* sql;

    sql = bypass_whitespace("SELECT");
    expect(*sql == 'S', "1");

    sql = bypass_whitespace(" SELECT");
    expect(*sql == 'S', "2");

    sql = bypass_whitespace("\tSELECT");
    expect(*sql == 'S', "3");

    sql = bypass_whitespace("\nSELECT");
    expect(*sql == 'S', "4");

    sql = bypass_whitespace("/* comment */SELECT");
    expect(*sql == 'S', "5");

    sql = bypass_whitespace(" /* comment */ SELECT");
    expect(*sql == 'S', "6");

    sql = bypass_whitespace("-- comment\nSELECT");
    expect(*sql == 'S', "7");

    sql = bypass_whitespace("-- comment\n /* comment */ SELECT");
    expect(*sql == 'S', "8");

    sql = bypass_whitespace("# comment\nSELECT");
    expect(*sql == 'S', "9");
}

int main(int argc, char** argv)
{
    test1();
    test2();
    test_single_sql_packet1();
    test_single_sql_packet2();
    test_multiple_sql_packets1();
    test_multiple_sql_packets2();
    test_strnchr_esc();
    test_strnchr_esc_mariadb();
    test_large_packets();
    test_bypass_whitespace();
    return retval;
}
