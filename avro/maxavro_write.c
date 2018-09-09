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

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "maxavro_internal.h"
#include <maxscale/log.h>
#include <errno.h>

/**
 * @file maxavro_write.c - Avro value writing
 *
 * Currently these functions aren't in use
 */

/**
 * @brief Encode an integer value in Avro format
 * @param buffer Buffer where the encoded value is stored
 * @param val Value to encode
 * @return Number of bytes encoded
 */
uint64_t maxavro_encode_integer(uint8_t* buffer, uint64_t val)
{
    uint64_t encval = encode_long(val);
    uint8_t nbytes = 0;

    while (more_bytes(encval))
    {
        buffer[nbytes++] = 0x80 | (0x7f & encval);
        encval >>= 7;
    }

    buffer[nbytes++] = encval;
    return nbytes;
}

bool maxavro_write_integer(FILE* file, uint64_t val)
{
    uint8_t buffer[MAX_INTEGER_SIZE];
    uint8_t nbytes = maxavro_encode_integer(buffer, val);
    return fwrite(buffer, 1, nbytes, file) == nbytes;
}


/**
 * @brief Encode a string in Avro format
 *
 * @param dest Destination buffer where the string is stored
 * @param str Null-terminated string to store
 * @return number of bytes stored
 */
uint64_t maxavro_encode_string(uint8_t* dest, const char* str)
{
    uint64_t slen = strlen(str);
    uint64_t ilen = maxavro_encode_integer(dest, slen);
    memcpy(dest, str, slen);
    return slen + ilen;
}

bool maxavro_write_string(FILE* file, const char* str)
{
    uint64_t len = strlen(str);
    return maxavro_write_integer(file, len) && fwrite(str, 1, len, file) == len;
}

/**
 * @brief Encode a float value in Avro format
 * @param buffer Buffer where the encoded value is stored
 * @param val Value to encode
 * @return Number of bytes encoded
 */
uint64_t maxavro_encode_float(uint8_t* dest, float val)
{
    memcpy(dest, &val, sizeof(val));
    return sizeof(val);
}

bool maxavro_write_float(FILE* file, float val)
{
    return fwrite(&val, 1, sizeof(val), file) == sizeof(val);
}

/**
 * @brief Encode a double value in Avro format
 * @param buffer Buffer where the encoded value is stored
 * @param val Value to encode
 * @return Number of bytes encoded
 */
uint64_t maxavro_encode_double(uint8_t* dest, double val)
{
    memcpy(dest, &val, sizeof(val));
    return sizeof(val);
}

bool maxavro_write_double(FILE* file, double val)
{
    return fwrite(&val, 1, sizeof(val), file) == sizeof(val);
}

MAXAVRO_MAP* avro_map_start()
{
    return (MAXAVRO_MAP*)calloc(1, sizeof(MAXAVRO_MAP));
}

uint64_t avro_map_encode(uint8_t* dest, MAXAVRO_MAP* map)
{
    uint64_t len = maxavro_encode_integer(dest, map->blocks);

    while (map)
    {
        len += maxavro_encode_string(dest, map->key);
        len += maxavro_encode_string(dest, map->value);
        map = map->next;
    }

    /** Maps end with an empty block i.e. a zero integer value */
    len += maxavro_encode_integer(dest, 0);
    return len;
}
