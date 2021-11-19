/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file mysql_binlog.h - Binary log constants
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

/** Maximum GTID string length */
#define GTID_MAX_LEN 64

/** Table map column types */
#define TABLE_COL_TYPE_DECIMAL     0x00
#define TABLE_COL_TYPE_TINY        0x01
#define TABLE_COL_TYPE_SHORT       0x02
#define TABLE_COL_TYPE_LONG        0x03
#define TABLE_COL_TYPE_FLOAT       0x04
#define TABLE_COL_TYPE_DOUBLE      0x05
#define TABLE_COL_TYPE_NULL        0x06
#define TABLE_COL_TYPE_TIMESTAMP   0x07
#define TABLE_COL_TYPE_LONGLONG    0x08
#define TABLE_COL_TYPE_INT24       0x09
#define TABLE_COL_TYPE_DATE        0x0a
#define TABLE_COL_TYPE_TIME        0x0b
#define TABLE_COL_TYPE_DATETIME    0x0c
#define TABLE_COL_TYPE_YEAR        0x0d
#define TABLE_COL_TYPE_NEWDATE     0x0e
#define TABLE_COL_TYPE_VARCHAR     0x0f
#define TABLE_COL_TYPE_BIT         0x10
#define TABLE_COL_TYPE_TIMESTAMP2  0x11
#define TABLE_COL_TYPE_DATETIME2   0x12
#define TABLE_COL_TYPE_TIME2       0x13
#define TABLE_COL_TYPE_NEWDECIMAL  0xf6
#define TABLE_COL_TYPE_ENUM        0xf7
#define TABLE_COL_TYPE_SET         0xf8
#define TABLE_COL_TYPE_TINY_BLOB   0xf9
#define TABLE_COL_TYPE_MEDIUM_BLOB 0xfa
#define TABLE_COL_TYPE_LONG_BLOB   0xfb
#define TABLE_COL_TYPE_BLOB        0xfc
#define TABLE_COL_TYPE_VAR_STRING  0xfd
#define TABLE_COL_TYPE_STRING      0xfe
#define TABLE_COL_TYPE_GEOMETRY    0xff

/**
 * RBR row event flags
 */
#define ROW_EVENT_END_STATEMENT 0x0001
#define ROW_EVENT_NO_FKCHECK    0x0002
#define ROW_EVENT_NO_UKCHECK    0x0004
#define ROW_EVENT_HAS_COLUMNS   0x0008

/** The table ID used for end of statement row events */
#define TABLE_DUMMY_ID 0x00ffffff

MXS_END_DECLS
