#pragma once
#ifndef _BINLOG_COMMON_H
#define _BINLOG_COMMON_H
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

#include <maxscale/cdefs.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

MXS_BEGIN_DECLS

/**
 * Packet header for replication messages
 */
typedef struct rep_header
{
    int      payload_len;   /*< Payload length (24 bits) */
    uint8_t  seqno;         /*< Response sequence number */
    uint8_t  ok;            /*< OK Byte from packet */
    uint32_t timestamp;     /*< Timestamp - start of binlog record */
    uint8_t  event_type;    /*< Binlog event type */
    uint32_t serverid;      /*< Server id of master */
    uint32_t event_size;    /*< Size of header, post-header and body */
    uint32_t next_pos;      /*< Position of next event */
    uint16_t flags;         /*< Event flags */
} REP_HEADER;

int         blr_file_get_next_binlogname(const char* binlog_name);
bool        binlog_next_file_exists(const char* binlogdir, const char* binlog);
uint32_t    extract_field(uint8_t* src, int bits);
const char* binlog_event_name(int type);

static inline REP_HEADER construct_header(uint8_t* ptr)
{
    REP_HEADER hdr;

    hdr.timestamp = extract_field(ptr, 32);
    hdr.event_type = ptr[4];
    hdr.serverid = extract_field(&ptr[5], 32);
    hdr.event_size = extract_field(&ptr[9], 32);
    hdr.next_pos = extract_field(&ptr[13], 32);
    hdr.flags = extract_field(&ptr[17], 16);

    return hdr;
}

MXS_END_DECLS

#endif /* BINLOG_COMMON_H */
