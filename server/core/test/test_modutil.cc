/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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
 * 17-09-2014   Martin Brampton     Initial implementation
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

#include <maxscale/alloc.h>
#include <maxscale/modutil.h>
#include <maxscale/buffer.h>

/**
 * test1    Allocate a service and do lots of other things
 *
 */

static int
test1()
{
    GWBUF   *buffer;
    char    *sql;
    int     result, length, residual;

    /* Poll tests */
    fprintf(stderr,
            "testmodutil : Rudimentary tests.");
    buffer = gwbuf_alloc(100);
    ss_info_dassert(GWBUF_IS_CONTIGUOUS(buffer), "Allocated buffer should be continuos");
    memset(GWBUF_DATA(buffer), 0, GWBUF_LENGTH(buffer));
    ss_info_dassert(0 == modutil_is_SQL(buffer), "Default buffer should be diagnosed as not SQL");
    /* There would ideally be some straightforward way to create a SQL buffer? */
    fprintf(stderr, "\t..done\nExtract SQL from buffer");
    ss_info_dassert(0 == modutil_extract_SQL(buffer, &sql, &length), "Default buffer should fail");
    fprintf(stderr, "\t..done\nExtract SQL from buffer different way?");
    ss_info_dassert(0 == modutil_MySQL_Query(buffer, &sql, &length, &residual), "Default buffer should fail");
    fprintf(stderr, "\t..done\nReplace SQL in buffer");
    ss_info_dassert(0 == modutil_replace_SQL(buffer, (char*)"select * from some_table;"),
                    "Default buffer should fail");
    fprintf(stderr, "\t..done\nTidy up.");
    gwbuf_free(buffer);
    fprintf(stderr, "\t..done\n");

    return 0;

}

int
test2()
{
    GWBUF   *buffer;
    unsigned int len = 128;
    char query[129];

    /** Allocate space for the COM_QUERY header and payload */
    buffer = gwbuf_alloc(5 + 128);
    ss_info_dassert((buffer != NULL), "Buffer should not be null");

    memset(query, ';', 128);
    memset(query + 128, '\0', 1);
    *((unsigned char*)buffer->start) = len;
    *((unsigned char*)buffer->start + 1) = 0;
    *((unsigned char*)buffer->start + 2) = 0;
    *((unsigned char*)buffer->start + 3) = 1;
    *((unsigned char*)buffer->start + 4) = 0x03;
    memcpy((uint8_t*)buffer->start + 5, query, strlen(query));
    char* result = modutil_get_SQL(buffer);
    ss_dassert(strcmp(result, query) == 0);
    gwbuf_free(buffer);
    MXS_FREE(result);
    fprintf(stderr, "\t..done\n");
    return 0;

}

/** This is a standard OK packet */
static char ok[] =
{
    0x07, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00
};

/** Created with:
 * CREATE OR REPLACE TABLE test.t1 (id int);
 * INSERT INTO test.t1 VALUES (3000);
 * SELECT * FROM test.t1; */
static const uint8_t resultset[] =
{
    /* Packet 1 */
    0x01, 0x00, 0x00, 0x01, 0x01,
    /* Packet 2 */
    0x22, 0x00, 0x00, 0x02, 0x03, 0x64, 0x65, 0x66, 0x04, 0x74, 0x65, 0x73, 0x74, 0x02, 0x74, 0x31,
    0x02, 0x74, 0x31, 0x02, 0x69, 0x64, 0x02, 0x69, 0x64, 0x0c, 0x3f,
    0x00, 0x0b, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Packet 3 */
    0x05, 0x00, 0x00, 0x03, 0xfe, 0x00, 0x00, 0x22, 0x00,
    /* Packet 4 */
    0x05, 0x00, 0x00, 0x04, 0x04, 0x33, 0x30, 0x30, 0x30,
    /* Packet 5 */
    0x05, 0x00, 0x00, 0x05, 0xfe, 0x00, 0x00, 0x22, 0x00
};

#define PACKET_HDR_LEN 4

#define PACKET_1_IDX 0
#define PACKET_1_LEN (PACKET_HDR_LEN + 0x01)       // resultset[PACKET_1_IDX])
#define PACKET_2_IDX (PACKET_1_IDX + PACKET_1_LEN)
#define PACKET_2_LEN (PACKET_HDR_LEN + 0x22)       // resultset[PACKET_2_IDX]);
#define PACKET_3_IDX (PACKET_2_IDX + PACKET_2_LEN)
#define PACKET_3_LEN (PACKET_HDR_LEN + 0x05)       // resultset[PACKET_3_IDX]);
#define PACKET_4_IDX (PACKET_3_IDX + PACKET_3_LEN)
#define PACKET_4_LEN (PACKET_HDR_LEN + 0x05)       // resultset[PACKET_4_IDX]);
#define PACKET_5_IDX (PACKET_4_IDX + PACKET_4_LEN)
#define PACKET_5_LEN (PACKET_HDR_LEN + 0x05)       // resultset[PACKET_5_IDX]);

struct packet
{
    int index;
    unsigned int length;
} packets[] =
{
    { PACKET_1_IDX, PACKET_1_LEN },
    { PACKET_2_IDX, PACKET_2_LEN },
    { PACKET_3_IDX, PACKET_3_LEN },
    { PACKET_4_IDX, PACKET_4_LEN },
    { PACKET_5_IDX, PACKET_5_LEN },
};

#define N_PACKETS (sizeof(packets)/sizeof(packets[0]))


//
// modutil_get_complete_packets
//
void test_single_sql_packet1()
{
    printf("%s\n", __func__);
    /** Single packet */
    GWBUF* buffer = gwbuf_alloc_and_load(sizeof(ok), ok);
    GWBUF* complete = modutil_get_complete_packets(&buffer);
    ss_info_dassert(buffer == NULL, "Old buffer should be NULL");
    ss_info_dassert(complete, "Complete packet buffer should not be NULL");
    ss_info_dassert(gwbuf_length(complete) == sizeof(ok), "Complete packet buffer should contain enough data");
    ss_info_dassert(memcmp(GWBUF_DATA(complete), ok, GWBUF_LENGTH(complete)) == 0,
                    "Complete packet buffer's data should be equal to original data");
    gwbuf_free(complete);

    /** Partial single packet */
    buffer = gwbuf_alloc_and_load(sizeof(ok) - 4, ok);
    complete = modutil_get_complete_packets(&buffer);
    ss_info_dassert(buffer, "Old buffer should be not NULL");
    ss_info_dassert(complete == NULL, "Complete packet buffer should be NULL");
    ss_info_dassert(gwbuf_length(buffer) == sizeof(ok) - 4, "Old buffer should contain right amount of data");

    /** Add the missing data */
    buffer = gwbuf_append(buffer, gwbuf_alloc_and_load(4, ok + sizeof(ok) - 4));
    complete = modutil_get_complete_packets(&buffer);
    ss_info_dassert(buffer == NULL, "Old buffer should be NULL");
    ss_info_dassert(complete, "Complete packet buffer should not be NULL");
    ss_info_dassert(complete->next, "The complete packet should be a chain of buffers");
    ss_info_dassert(gwbuf_length(complete) == sizeof(ok), "Buffer should contain all data");
    gwbuf_free(complete);
}

void test_multiple_sql_packets1()
{
    printf("%s\n", __func__);
    /** All of the data */
    GWBUF* buffer = gwbuf_alloc_and_load(sizeof(resultset), resultset);
    GWBUF* complete = modutil_get_complete_packets(&buffer);
    ss_info_dassert(buffer == NULL, "Old buffer should be NULL");
    ss_info_dassert(complete, "Complete packet buffer should not be NULL");
    ss_info_dassert(gwbuf_length(complete) == sizeof(resultset),
                    "Complete packet buffer should contain enough data");
    ss_info_dassert(memcmp(GWBUF_DATA(complete), resultset, GWBUF_LENGTH(complete)) == 0,
                    "Complete packet buffer's data should be equal to original data");
    gwbuf_free(complete);

    /** Partial data available with one complete packet */
    GWBUF* head = gwbuf_alloc_and_load(7, resultset);
    GWBUF* tail = gwbuf_alloc_and_load(sizeof(resultset) - 7, resultset + 7);
    complete = modutil_get_complete_packets(&head);
    ss_info_dassert(head, "Old buffer should not be NULL");
    ss_info_dassert(complete, "Complete buffer should not be NULL");
    ss_info_dassert(gwbuf_length(complete) == 5, "Complete buffer should contain first packet only");
    ss_info_dassert(gwbuf_length(head) == 2, "Complete buffer should contain first packet only");
    gwbuf_free(complete);

    /** All packets are available */
    head = gwbuf_append(head, tail);
    complete = modutil_get_complete_packets(&head);
    ss_info_dassert(head == NULL, "Old buffer should be NULL");
    ss_info_dassert(complete, "Complete packet buffer should not be NULL");
    ss_info_dassert(gwbuf_length(complete) == sizeof(resultset) - 5,
                    "Complete packet should be sizeof(resultset) - 5 bytes");
    gwbuf_free(complete);

    /** Sliding cutoff of the buffer boundary */
    for (size_t i = 0; i < sizeof(resultset); i++)
    {
        head = gwbuf_alloc_and_load(i, resultset);
        tail = gwbuf_alloc_and_load(sizeof(resultset) - i, resultset + i);
        head = gwbuf_append(head, tail);
        complete = modutil_get_complete_packets(&head);
        int headlen = gwbuf_length(head);
        int completelen = complete ? gwbuf_length(complete) : 0;
        ss_info_dassert(headlen + completelen == sizeof(resultset),
                        "Both buffers should sum up to sizeof(resutlset) bytes");
        uint8_t databuf[sizeof(resultset)];
        gwbuf_copy_data(complete, 0, completelen, databuf);
        gwbuf_copy_data(head, 0, headlen, databuf + completelen);
        ss_info_dassert(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");
        gwbuf_free(head);
        gwbuf_free(complete);
    }

    /** Fragmented buffer chain */
    size_t chunk = 5;
    size_t total = 0;
    head = NULL;

    do
    {
        chunk = chunk + 5 < sizeof(resultset) ? 5 : (chunk + 5) - sizeof(resultset);
        head = gwbuf_append(head, gwbuf_alloc_and_load(chunk, resultset + total));
        total += chunk;
    }
    while (total < sizeof(resultset));

    ss_info_dassert(gwbuf_length(head) == sizeof(resultset), "Head should be sizeof(resulset) bytes long");
    complete = modutil_get_complete_packets(&head);
    ss_info_dassert(head == NULL, "Head should be NULL");
    ss_info_dassert(complete, "Complete should not be NULL");
    ss_info_dassert(gwbuf_length(complete) == sizeof(resultset),
                    "Complete should be sizeof(resulset) bytes long");

    unsigned int headlen = gwbuf_length(head);
    unsigned int completelen = complete ? gwbuf_length(complete) : 0;
    uint8_t databuf[sizeof(resultset)];
    ss_info_dassert(gwbuf_copy_data(complete, 0, completelen, databuf) == completelen,
                    "Expected data should be readable");
    ss_info_dassert(gwbuf_copy_data(head, 0, headlen, databuf + completelen) == headlen,
                    "Expected data should be readable");
    ss_info_dassert(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");

    /** Fragmented buffer split into multiple chains and then reassembled as a complete resultset */
    GWBUF* half = complete;
    GWBUF* quarter = gwbuf_split(&half, gwbuf_length(half) / 2);
    head = gwbuf_split(&quarter, gwbuf_length(quarter) / 2);
    ss_info_dassert(half && quarter && head, "gwbuf_split should work");

    complete = modutil_get_complete_packets(&head);
    ss_info_dassert(complete && head, "Both buffers should have data");
    ss_info_dassert(gwbuf_length(complete) + gwbuf_length(head) + gwbuf_length(quarter)
                    + gwbuf_length(half) == sizeof(resultset), "25% of data should be available");

    quarter = gwbuf_append(gwbuf_append(complete, head), quarter);
    complete = modutil_get_complete_packets(&quarter);
    ss_info_dassert(gwbuf_length(complete) + gwbuf_length(quarter) +
                    gwbuf_length(half) == sizeof(resultset), "50% of data should be available");

    half = gwbuf_append(gwbuf_append(complete, quarter), half);
    complete = modutil_get_complete_packets(&half);
    ss_info_dassert(complete, "Complete should not be NULL");
    ss_info_dassert(half == NULL, "Old buffer should be NULL");
    ss_info_dassert(gwbuf_length(complete) == sizeof(resultset), "Complete should contain 100% of data");

    completelen = gwbuf_length(complete);
    ss_info_dassert(gwbuf_copy_data(complete, 0, completelen, databuf) == completelen,
                    "All data should be readable");
    ss_info_dassert(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");
    gwbuf_free(complete);
}

//
// modutil_get_next_MySQL_packet
//
void test_single_sql_packet2()
{
    printf("%s\n", __func__);
    /** Single packet */
    GWBUF* buffer;
    GWBUF* next;

    buffer = gwbuf_alloc_and_load(sizeof(ok), ok);
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer == NULL, "Old buffer should be NULL");
    ss_info_dassert(next, "Next packet buffer should not be NULL");
    ss_info_dassert(gwbuf_length(next) == sizeof(ok), "Next packet buffer should contain enough data");
    ss_info_dassert(memcmp(GWBUF_DATA(next), ok, GWBUF_LENGTH(next)) == 0,
                    "Next packet buffer's data should be equal to original data");
    gwbuf_free(buffer);
    gwbuf_free(next);

    /** Partial single packet */
    buffer = gwbuf_alloc_and_load(sizeof(ok) - 4, ok);
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer, "Old buffer should be not NULL");
    ss_info_dassert(next == NULL, "Next packet buffer should be NULL");
    ss_info_dassert(gwbuf_length(buffer) == sizeof(ok) - 4, "Old buffer should contain right amount of data");

    /** Add the missing data */
    buffer = gwbuf_append(buffer, gwbuf_alloc_and_load(4, ok + sizeof(ok) - 4));
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer == NULL, "Old buffer should be NULL");
    ss_info_dassert(next, "Next packet buffer should not be NULL");
    //To be put back when the current realloc behaviour is replaced with splitting behaviour.
    //ss_info_dassert(next->next, "The next packet should be a chain of buffers");
    ss_info_dassert(gwbuf_length(next) == sizeof(ok), "Buffer should contain all data");
    gwbuf_free(next);
}

void test_multiple_sql_packets2()
{
    printf("%s\n", __func__);
    /** All of the data */
    GWBUF* buffer;
    GWBUF* next;

    buffer = gwbuf_alloc_and_load(sizeof(resultset), resultset);
    // Empty buffer packet by packet.
    for (unsigned int i = 0; i < N_PACKETS; i++)
    {
        GWBUF* next = modutil_get_next_MySQL_packet(&buffer);
        ss_info_dassert(next, "Next packet buffer should not be NULL");
        ss_info_dassert(gwbuf_length(next) == packets[i].length,
                        "Next packet buffer should contain enough data");
        ss_info_dassert(memcmp(GWBUF_DATA(next), &resultset[packets[i].index], GWBUF_LENGTH(next)) == 0,
                        "Next packet buffer's data should be equal to original data");
        gwbuf_free(next);
    }
    ss_info_dassert(buffer == NULL, "Buffer should be NULL");

    size_t len;
    // Exactly one packet
    len = PACKET_1_LEN;
    buffer = gwbuf_alloc_and_load(len, resultset);
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer == NULL, "Old buffer should be NULL.");
    ss_info_dassert(next, "Next should not be NULL.");
    ss_info_dassert(GWBUF_LENGTH(next) == PACKET_1_LEN, "Length should match.");
    gwbuf_free(next);

    // Slightly less than one packet
    len = PACKET_1_LEN - 1;
    buffer = gwbuf_alloc_and_load(len, resultset);
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer, "Old buffer should not be NULL.");
    ss_info_dassert(next == NULL, "Next should be NULL.");

    GWBUF *tail = gwbuf_alloc_and_load(sizeof(resultset) - len, resultset + len);
    buffer = gwbuf_append(buffer, tail);
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer, "Old buffer should not be NULL.");
    ss_info_dassert(next, "Next should not be NULL.");
    ss_info_dassert(gwbuf_length(next) == PACKET_1_LEN, "Length should match.");
    gwbuf_free(buffer);
    gwbuf_free(next);

    // Slightly more than one packet
    len = PACKET_1_LEN + 1;
    buffer = gwbuf_alloc_and_load(len, resultset);
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer, "Old buffer should not be NULL.");
    ss_info_dassert(next, "Next should not be NULL.");
    ss_info_dassert(GWBUF_LENGTH(next) == PACKET_1_LEN, "Length should match.");
    gwbuf_free(next);

    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer, "Old buffer should not be NULL.");
    ss_info_dassert(next == NULL, "Next should be NULL.");

    tail = gwbuf_alloc_and_load(sizeof(resultset) - len, resultset + len);
    buffer = gwbuf_append(buffer, tail);
    next = modutil_get_next_MySQL_packet(&buffer);
    ss_info_dassert(buffer, "Old buffer should not be NULL.");
    ss_info_dassert(next, "Next should not be NULL.");
    ss_info_dassert(gwbuf_length(next) == PACKET_2_LEN, "Length should match.");
    ss_info_dassert(memcmp(GWBUF_DATA(next), &resultset[PACKET_2_IDX], GWBUF_LENGTH(next)) == 0,
                    "Next packet buffer's data should be equal to original data");
    gwbuf_free(buffer);
    gwbuf_free(next);

    GWBUF *head;
    /** Sliding cutoff of the buffer boundary */
    for (size_t i = 0; i < sizeof(resultset); i++)
    {
        head = gwbuf_alloc_and_load(i, resultset);
        tail = gwbuf_alloc_and_load(sizeof(resultset) - i, resultset + i);
        head = gwbuf_append(head, tail);
        next = modutil_get_next_MySQL_packet(&head);
        int headlen = gwbuf_length(head);
        int nextlen = next ? gwbuf_length(next) : 0;
        ss_info_dassert(headlen + nextlen == sizeof(resultset),
                        "Both buffers should sum up to sizeof(resutlset) bytes");
        uint8_t databuf[sizeof(resultset)];
        gwbuf_copy_data(next, 0, nextlen, databuf);
        gwbuf_copy_data(head, 0, headlen, databuf + nextlen);
        ss_info_dassert(memcmp(databuf, resultset, sizeof(resultset)) == 0, "Data should be OK");
        gwbuf_free(head);
        gwbuf_free(next);
    }

    /** Fragmented buffer chain */
    size_t chunk = 5;
    size_t total = 0;
    buffer = NULL;

    do
    {
        chunk = chunk + 5 < sizeof(resultset) ? 5 : (chunk + 5) - sizeof(resultset);
        buffer = gwbuf_append(buffer, gwbuf_alloc_and_load(chunk, resultset + total));
        total += chunk;
    }
    while (total < sizeof(resultset));

    for (unsigned int i = 0; i < N_PACKETS; i++)
    {
        GWBUF* next = modutil_get_next_MySQL_packet(&buffer);
        ss_info_dassert(next, "Next packet buffer should not be NULL");
        ss_info_dassert(gwbuf_length(next) == packets[i].length,
                        "Next packet buffer should contain enough data");
        next = gwbuf_make_contiguous(next);
        ss_info_dassert(memcmp(GWBUF_DATA(next), &resultset[packets[i].index], GWBUF_LENGTH(next)) == 0,
                        "Next packet buffer's data should be equal to original data");
        gwbuf_free(next);
    }
    ss_info_dassert(buffer == NULL, "Buffer should be NULL");
}

void test_strnchr_esc_mysql()
{
    char comment1[] = "This will -- fail.";
    ss_info_dassert(strnchr_esc_mysql(comment1, '.', sizeof(comment1) - 1) == NULL,
                    "Commented character should return NULL");

    char comment2[] = "This will # fail.";
    ss_info_dassert(strnchr_esc_mysql(comment2, '.', sizeof(comment2) - 1) == NULL,
                    "Commented character should return NULL");

    char comment3[] = "This will fail/* . */";
    ss_info_dassert(strnchr_esc_mysql(comment3, '.', sizeof(comment3) - 1) == NULL,
                    "Commented character should return NULL");

    char comment4[] = "This will not /* . */ fail.";
    ss_info_dassert(strnchr_esc_mysql(comment4, '.', sizeof(comment4) - 1) == strrchr(comment4, '.'),
                    "Uncommented character should be matched");

    char comment5[] = "This will fail/* . ";
    ss_info_dassert(strnchr_esc_mysql(comment5, '.', sizeof(comment5) - 1) == NULL, "Bad comment should fail");

}

void test_strnchr_esc()
{
    /** Single escaped and quoted characters */
    char esc1[] = "This will fail\\.";
    ss_info_dassert(strnchr_esc(esc1, '.', sizeof(esc1) - 1) == NULL,
                    "Only escaped character should return NULL");
    ss_info_dassert(strnchr_esc_mysql(esc1, '.', sizeof(esc1) - 1) == NULL,
                    "Only escaped character should return NULL");

    char esc2[] = "This will fail\".\"";
    ss_info_dassert(strnchr_esc(esc1, '.', sizeof(esc1) - 1) == NULL,
                    "Only escaped character should return NULL");
    ss_info_dassert(strnchr_esc_mysql(esc1, '.', sizeof(esc1) - 1) == NULL,
                    "Only escaped character should return NULL");

    char esc3[] = "This will fail'.'";
    ss_info_dassert(strnchr_esc(esc1, '.', sizeof(esc1) - 1) == NULL,
                    "Only escaped character should return NULL");
    ss_info_dassert(strnchr_esc_mysql(esc1, '.', sizeof(esc1) - 1) == NULL,
                    "Only escaped character should return NULL");

    /** Test escaped and quoted characters */
    char str1[] = "this \\. is a test.";
    ss_info_dassert(strnchr_esc(str1, '.', sizeof(str1) - 1) == strrchr(str1, '.'),
                    "Escaped characters should be ignored");
    ss_info_dassert(strnchr_esc_mysql(str1, '.', sizeof(str1) - 1) == strrchr(str1, '.'),
                    "Escaped characters should be ignored");
    char str2[] = "this \"is . \" a test .";
    ss_info_dassert(strnchr_esc(str2, '.', sizeof(str2) - 1) == strrchr(str2, '.'),
                    "Double quoted characters should be ignored");
    ss_info_dassert(strnchr_esc_mysql(str2, '.', sizeof(str2) - 1) == strrchr(str2, '.'),
                    "Double quoted characters should be ignored");
    char str3[] = "this 'is . ' a test .";
    ss_info_dassert(strnchr_esc(str3, '.', sizeof(str3) - 1) == strrchr(str3, '.'),
                    "Double quoted characters should be ignored");
    ss_info_dassert(strnchr_esc_mysql(str3, '.', sizeof(str3) - 1) == strrchr(str3, '.'),
                    "Double quoted characters should be ignored");

    /** Bad quotation tests */
    char bad1[] = "This will \" fail.";
    ss_info_dassert(strnchr_esc(bad1, '.', sizeof(bad1) - 1) == NULL, "Bad quotation should fail");
    ss_info_dassert(strnchr_esc_mysql(bad1, '.', sizeof(bad1) - 1) == NULL, "Bad quotation should fail");

    char bad2[] = "This will ' fail.";
    ss_info_dassert(strnchr_esc(bad2, '.', sizeof(bad2) - 1) == NULL, "Bad quotation should fail");
    ss_info_dassert(strnchr_esc_mysql(bad2, '.', sizeof(bad2) - 1) == NULL, "Bad quotation should fail");

    char bad3[] = "This will \" fail. '";
    ss_info_dassert(strnchr_esc(bad3, '.', sizeof(bad3) - 1) == NULL, "Different quote pairs should fail");
    ss_info_dassert(strnchr_esc_mysql(bad3, '.', sizeof(bad3) - 1) == NULL, "Different quote pairs should fail");

    char bad4[] = "This will ' fail. \"";
    ss_info_dassert(strnchr_esc(bad4, '.', sizeof(bad4) - 1) == NULL, "Different quote pairs should fail");
    ss_info_dassert(strnchr_esc_mysql(bad4, '.', sizeof(bad4) - 1) == NULL, "Different quote pairs should fail");
}

GWBUF* create_buffer(size_t size)
{
    GWBUF* buffer = gwbuf_alloc(size + 4);
    uint8_t* data = (uint8_t*)GWBUF_DATA(buffer);
    *(data + 0) = size;
    *(data + 1) = size >> 8;
    *(data + 2) = size >> 16;
    *(data + 3) = 0;
    return buffer;
}

void test_large_packets()
{
    /** Two complete large packets */
    for (int i = -4; i < 5; i++)
    {
        unsigned long ul = 0x00ffffff + i;
        size_t first_len = ul > 0x00ffffff ? 0x00ffffff : ul;
        GWBUF* buffer = create_buffer(first_len);

        if (first_len < ul)
        {
            buffer = gwbuf_append(buffer, create_buffer(ul - first_len));
        }
        size_t before = gwbuf_length(buffer);
        GWBUF* complete = modutil_get_complete_packets(&buffer);

        ss_info_dassert(buffer == NULL, "Original buffer should be NULL");
        ss_info_dassert(complete, "Complete buffer should not be NULL");
        ss_info_dassert(gwbuf_length(complete) == before, "Complete buffer should contain all data");
        gwbuf_free(complete);
    }

    /** Incomplete packet */
    for (int i = 0; i < 5; i++)
    {
        GWBUF* buffer = create_buffer(0x00ffffff - i);
        buffer = gwbuf_rtrim(buffer, 4);
        GWBUF* complete = modutil_get_complete_packets(&buffer);
        ss_info_dassert(buffer, "Incomplete buffer is not NULL");
        ss_info_dassert(complete == NULL, "The complete buffer is NULL");
        gwbuf_free(buffer);
    }

    /** Incomplete second packet */
    for (int i = 2; i < 8; i++)
    {
        GWBUF* buffer = gwbuf_append(create_buffer(0x00ffffff), create_buffer(i));
        ss_dassert(gwbuf_length(buffer) == 0xffffffUL + i + 8);
        GWBUF_RTRIM(buffer->next, 1)
        GWBUF* complete = modutil_get_complete_packets(&buffer);
        ss_info_dassert(buffer, "Incomplete buffer is not NULL");
        ss_info_dassert(complete, "The complete buffer is not NULL");
        ss_info_dassert(gwbuf_length(complete) == 0xffffff + 4, "Length should be correct");
        gwbuf_free(buffer);
        gwbuf_free(complete);
    }
}

char* bypass_whitespace(const char* sql)
{
    return modutil_MySQL_bypass_whitespace((char*)sql, strlen(sql));
}

void test_bypass_whitespace()
{
    char* sql;

    sql = bypass_whitespace("SELECT");
    ss_info_dassert(*sql == 'S', "1");

    sql = bypass_whitespace(" SELECT");
    ss_info_dassert(*sql == 'S', "2");

    sql = bypass_whitespace("\tSELECT");
    ss_info_dassert(*sql == 'S', "3");

    sql = bypass_whitespace("\nSELECT");
    ss_info_dassert(*sql == 'S', "4");

    sql = bypass_whitespace("/* comment */SELECT");
    ss_info_dassert(*sql == 'S', "5");

    sql = bypass_whitespace(" /* comment */ SELECT");
    ss_info_dassert(*sql == 'S', "6");

    sql = bypass_whitespace("-- comment\nSELECT");
    ss_info_dassert(*sql == 'S', "7");

    sql = bypass_whitespace("-- comment\n /* comment */ SELECT");
    ss_info_dassert(*sql == 'S', "8");

    sql = bypass_whitespace("# comment\nSELECT");
    ss_info_dassert(*sql == 'S', "9");
}

int main(int argc, char **argv)
{
    int result = 0;

    result += test1();
    result += test2();
    test_single_sql_packet1();
    test_single_sql_packet2();
    test_multiple_sql_packets1();
    test_multiple_sql_packets2();
    test_strnchr_esc();
    test_strnchr_esc_mysql();
    test_large_packets();
    test_bypass_whitespace();
    exit(result);
}
