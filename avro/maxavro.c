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
#include <maxscale/log_manager.h>
#include <errno.h>

/** Maximum byte size of an integer value */
#define MAX_INTEGER_SIZE 10

#define avro_decode(n) ((n >> 1) ^ -(n & 1))
#define encode_long(n) ((n << 1) ^ (n >> 63))
#define more_bytes(b) (b & 0x80)

/**
 * @brief Read an Avro integer
 *
 * The integer lengths are all variable and the last bit in a byte indicates
 * if more bytes belong to the integer value. The real value of the integer is
 * the concatenation of the lowest seven bits of each byte. This value is encoded
 * in a zigzag patten i.e. first value is -1, second 1, third -2 and so on.
 * @param file The source FILE handle
 * @param dest Destination where the read value is written
 * @return True if value was read successfully
 */
bool maxavro_read_integer(MAXAVRO_FILE* file, uint64_t *dest)
{
    uint64_t rval = 0;
    uint8_t nread = 0;
    uint8_t byte;
    do
    {
        if (nread >= MAX_INTEGER_SIZE)
        {
            file->last_error = MAXAVRO_ERR_VALUE_OVERFLOW;
            return false;
        }

        if (file->buffer_ptr < file->buffer_end)
        {
            byte = *file->buffer_ptr;
            file->buffer_ptr++;
        }
        else
        {
            return false;
        }
        rval |= (uint64_t)(byte & 0x7f) << (nread++ * 7);
    }
    while (more_bytes(byte));

    if (dest)
    {
        *dest = avro_decode(rval);
    }
    return true;
}

bool maxavro_read_integer_from_file(MAXAVRO_FILE* file, uint64_t *dest)
{
    uint64_t rval = 0;
    uint8_t nread = 0;
    uint8_t byte;
    do
    {
        if (nread >= MAX_INTEGER_SIZE)
        {
            file->last_error = MAXAVRO_ERR_VALUE_OVERFLOW;
            return false;
        }
        size_t rdsz = fread(&byte, sizeof(byte), 1, file->file);
        if (rdsz != sizeof(byte))
        {
            if (rdsz != 0)
            {
                MXS_ERROR("Failed to read %lu bytes from '%s'", sizeof(byte), file->filename);
                file->last_error = MAXAVRO_ERR_IO;
            }
            else
            {
                MXS_DEBUG("Read 0 bytes from file '%s'", file->filename);
            }
            return false;
        }
        rval |= (uint64_t)(byte & 0x7f) << (nread++ * 7);
    }
    while (more_bytes(byte));

    if (dest)
    {
        *dest = avro_decode(rval);
    }
    return true;
}

/**
 * @brief Calculate the length of an Avro integer
 *
 * @param val Vale to calculate
 * @return Length of the value in bytes
 */
uint64_t avro_length_integer(uint64_t val)
{
    uint64_t encval = encode_long(val);
    uint8_t nbytes = 0;

    while (more_bytes(encval))
    {
        nbytes++;
        encval >>= 7;
    }

    return nbytes;
}

/**
 * @brief Read an Avro string
 *
 * The strings are encoded as one Avro integer followed by that many bytes of
 * data.
 * @param file File to read from
 * @return Pointer to newly allocated string or NULL if an error occurred
 *
 * @see maxavro_get_error
 */
char* maxavro_read_string(MAXAVRO_FILE* file, size_t* size)
{
    char *key = NULL;
    uint64_t len;

    if (maxavro_read_integer(file, &len))
    {
        key = MXS_MALLOC(len + 1);
        if (key)
        {
            memcpy(key, file->buffer_ptr, len);
            key[len] = '\0';
            file->buffer_ptr += len;
            *size = len;
        }
        else
        {
            file->last_error = MAXAVRO_ERR_MEMORY;
        }
    }
    return key;
}


/**
 * @brief Read an Avro string
 *
 * The strings are encoded as one Avro integer followed by that many bytes of
 * data.
 * @param file File to read from
 * @return Pointer to newly allocated string or NULL if an error occurred
 *
 * @see maxavro_get_error
 */
char* maxavro_read_string_from_file(MAXAVRO_FILE* file, size_t* size)
{
    char *key = NULL;
    uint64_t len;

    if (maxavro_read_integer_from_file(file, &len))
    {
        key = MXS_MALLOC(len + 1);
        if (key)
        {
            if (fread(key, 1, len, file->file) == len)
            {
                key[len] = '\0';
                *size = len;
            }
            else
            {
                file->last_error = MAXAVRO_ERR_IO;
                MXS_FREE(key);
                key = NULL;
            }
        }
        else
        {
            file->last_error = MAXAVRO_ERR_MEMORY;
        }
    }
    return key;
}

/**
 * @bref Skip an Avro string
 *
 * @param file Avro file handle
 * @return True if the string was skipped, false if an error occurred.
 * @see maxavro_get_error
 */
bool maxavro_skip_string(MAXAVRO_FILE* file)
{
    uint64_t len;

    if (maxavro_read_integer(file, &len))
    {
        file->buffer_ptr += len;
        return true;
    }

    return false;
}

/**
 * @brief Calculate the length of an Avro string
 * @param val Vale to calculate
 * @return Length of the string in bytes
 */
uint64_t avro_length_string(const char* str)
{
    uint64_t slen = strlen(str);
    uint64_t ilen = avro_length_integer(slen);
    return slen + ilen;
}

/**
 * @brief Read an Avro float
 *
 * The float is encoded as a 4 byte floating point value
 * @param file File to read from
 * @param dest Destination where the read value is stored
 * @return True if value was read successfully, false if an error occurred
 *
 * @see maxavro_get_error
 */
bool maxavro_read_float(MAXAVRO_FILE* file, float *dest)
{
    bool rval = false;

    if (file->buffer_ptr + sizeof(*dest) <= file->buffer_end)
    {
        memcpy(dest, file->buffer_ptr, sizeof(*dest));
        file->buffer_ptr += sizeof(*dest);
        rval = true;
    }
    else
    {
        ss_dassert(!true);
        MXS_ERROR("Block cannot hold a value of type float");
    }

    return rval;
}

/**
 * @brief Calculate the length of a float value
 * @param val Vale to calculate
 * @return Length of the value in bytes
 */
uint64_t avro_length_float(float val)
{
    return sizeof(val);
}

/**
 * @brief Read an Avro double
 *
 * The float is encoded as a 8 byte floating point value
 * @param file File to read from
 * @param dest Destination where the read value is stored
 * @return True if value was read successfully, false if an error occurred
 *
 * @see maxavro_get_error
 */
bool maxavro_read_double(MAXAVRO_FILE* file, double *dest)
{
    bool rval = false;

    if (file->buffer_ptr + sizeof(*dest) <= file->buffer_end)
    {
        memcpy(dest, file->buffer_ptr, sizeof(*dest));
        file->buffer_ptr += sizeof(*dest);
        rval = true;
    }
    else
    {
        ss_dassert(!true);
        MXS_ERROR("Block cannot hold a value of type double");
    }

    return rval;
}

/**
 * @brief Calculate the length of a double value
 * @param val Vale to calculate
 * @return Length of the value in bytes
 */
uint64_t avro_length_double(double val)
{
    return sizeof(val);
}

/**
 * @brief Read an Avro map
 *
 * A map is encoded as a series of blocks. Each block is encoded as an Avro
 * integer followed by that many key-value pairs of Avro strings. The last
 * block in the map will be a zero length block signaling its end.
 * @param file File to read from
 * @return A read map or NULL if an error occurred. The return value needs to be
 * freed with maxavro_map_free().
 */
MAXAVRO_MAP* maxavro_read_map_from_file(MAXAVRO_FILE *file)
{
    MAXAVRO_MAP* rval = NULL;
    uint64_t blocks;

    if (!maxavro_read_integer_from_file(file, &blocks))
    {
        return NULL;
    }

    while (blocks > 0)
    {
        for (long i = 0; i < blocks; i++)
        {
            size_t size;
            MAXAVRO_MAP* val = calloc(1, sizeof(MAXAVRO_MAP));
            if (val && (val->key = maxavro_read_string_from_file(file, &size)) && (val->value = maxavro_read_string_from_file(file, &size)))
            {
                val->next = rval;
                rval = val;
            }
            else
            {
                maxavro_map_free(val);
                maxavro_map_free(rval);
                return NULL;
            }
        }
        if (!maxavro_read_integer_from_file(file, &blocks))
        {
            maxavro_map_free(rval);
            return NULL;
        }
    }

    return rval;
}

/**
 * @brief Free an Avro map
 *
 * @param value Map to free
 */
void maxavro_map_free(MAXAVRO_MAP *value)
{
    while (value)
    {
        MAXAVRO_MAP* tmp = value;
        value = value->next;
        MXS_FREE(tmp->key);
        MXS_FREE(tmp->value);
        MXS_FREE(tmp);
    }
}

/**
 * @brief Calculate the length of an Avro map
 * @param map Map to measure
 * @return Length of the map in bytes
 */
uint64_t avro_map_length(MAXAVRO_MAP* map)
{
    uint64_t len = avro_length_integer(map->blocks);

    while (map)
    {
        len += avro_length_string(map->key);
        len += avro_length_string(map->value);
        map = map->next;
    }

    len += avro_length_integer(0);
    return len;
}
