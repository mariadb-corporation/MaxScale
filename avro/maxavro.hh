/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <jansson.h>
#include <maxscale/buffer.hh>

/** File magic and sync marker sizes block sizes */
#define AVRO_MAGIC_SIZE  4
#define SYNC_MARKER_SIZE 16

/** The file magic */
static const char avro_magic[] = {0x4f, 0x62, 0x6a, 0x01};

enum maxavro_value_type
{
    MAXAVRO_TYPE_UNKNOWN = 0,
    MAXAVRO_TYPE_INT,
    MAXAVRO_TYPE_LONG,
    MAXAVRO_TYPE_FLOAT,
    MAXAVRO_TYPE_DOUBLE,
    MAXAVRO_TYPE_BOOL,
    MAXAVRO_TYPE_STRING,
    MAXAVRO_TYPE_BYTES,
    MAXAVRO_TYPE_ENUM,
    MAXAVRO_TYPE_NULL,
    MAXAVRO_TYPE_UNION,
    MAXAVRO_TYPE_MAX
};

typedef struct
{
    char*                   name;
    void*                   extra;
    enum maxavro_value_type type;
} MAXAVRO_SCHEMA_FIELD;

typedef struct
{
    MAXAVRO_SCHEMA_FIELD* fields;
    size_t                num_fields;
} MAXAVRO_SCHEMA;

enum maxavro_codec
{
    MAXAVRO_CODEC_NULL,
    MAXAVRO_CODEC_DEFLATE,
    MAXAVRO_CODEC_SNAPPY,   /**< Not yet implemented */
};

enum maxavro_error
{
    MAXAVRO_ERR_NONE,
    MAXAVRO_ERR_IO,
    MAXAVRO_ERR_MEMORY,
    MAXAVRO_ERR_VALUE_OVERFLOW
};

typedef struct
{
    FILE*              file;
    char*              filename;/*< The filename */
    MAXAVRO_SCHEMA*    schema;
    enum maxavro_codec codec;
    uint64_t           blocks_read; /*< Total number of data blocks read */
    uint64_t           records_read;/*< Total number of records read */
    uint64_t           bytes_read;  /*< Total number of bytes read */
    uint64_t           records_in_block;
    uint64_t           records_read_from_block;
    uint64_t           bytes_read_from_block;
    uint64_t           buffer_size; /*< Size of the block in bytes */
    uint8_t*           buffer;      /**< The uncompressed data */
    uint8_t*           buffer_end;  /**< The byte after the end of the buffer*/
    uint8_t*           buffer_ptr;  /**< Pointer to @c buffer which is moved as records are read */
    /** The position @c ftell returns before the first record is read  */
    long header_end_pos;
    long data_start_pos;
    long block_start_pos;
    bool metadata_read;             /*< If datablock metadata has been read. This is kept
                                     * in memory if EOF is reached but an attempt to read
                                     * is made later when new data is available. We need
                                     * to know when to read it and when not to.  */
    enum maxavro_error last_error;  /*< Last error */
    uint8_t            sync[SYNC_MARKER_SIZE];
} MAXAVRO_FILE;

/** A record field value */
typedef union
{
    uint64_t integer;
    double   floating;
    char*    string;
    bool     boolean;
    void*    bytes;
} MAXAVRO_RECORD_VALUE;

/** A record value */
typedef struct
{
    MAXAVRO_SCHEMA_FIELD* field;
    MAXAVRO_RECORD_VALUE* value;
    size_t                size;
} MAXAVRO_RECORD;

typedef struct
{
    uint8_t*      buffer;       /*< Buffer memory */
    size_t        buffersize;   /*< Size of the buffer */
    size_t        datasize;     /*< size of written data */
    uint64_t      records;      /*< Number of successfully written records */
    MAXAVRO_FILE* avrofile;     /*< The current open file */
} MAXAVRO_DATABLOCK;

typedef struct avro_map_value
{
    char*                  key;
    char*                  value;
    struct avro_map_value* next;
    struct avro_map_value* tail;
    int                    blocks;  /*< Number of added key-value blocks */
} MAXAVRO_MAP;

/** Opening and closing files */
MAXAVRO_FILE* maxavro_file_open(const char* filename);
void          maxavro_file_close(MAXAVRO_FILE* file);

/** Reading records */
json_t* maxavro_record_read_json(MAXAVRO_FILE* file);
GWBUF*  maxavro_record_read_binary(MAXAVRO_FILE* file);

/** Navigation of the file */
bool maxavro_record_seek(MAXAVRO_FILE* file, uint64_t offset);
bool maxavro_record_set_pos(MAXAVRO_FILE* file, long pos);
bool maxavro_next_block(MAXAVRO_FILE* file);

/** Get binary format header */
GWBUF* maxavro_file_binary_header(MAXAVRO_FILE* file);

/** File error functions */
enum maxavro_error maxavro_get_error(MAXAVRO_FILE* file);
const char*        maxavro_get_error_string(MAXAVRO_FILE* file);
