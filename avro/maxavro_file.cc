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

#include "maxavro_internal.h"
#include <errno.h>
#include <string.h>
#include <maxbase/assert.h>
#include <maxscale/log.h>
#include <zlib.h>

static bool maxavro_read_sync(FILE* file, uint8_t* sync)
{
    bool rval = true;

    if (fread(sync, 1, SYNC_MARKER_SIZE, file) != SYNC_MARKER_SIZE)
    {
        rval = false;

        if (ferror(file))
        {
            MXS_ERROR("Failed to read file sync marker: %d, %s",
                      errno,
                      mxs_strerror(errno));
        }
        else if (feof(file))
        {
            MXS_ERROR("Short read when reading file sync marker.");
        }
        else
        {
            MXS_ERROR("Unspecified error when reading file sync marker.");
        }
    }

    return rval;
}

bool maxavro_verify_block(MAXAVRO_FILE* file)
{
    char sync[SYNC_MARKER_SIZE];
    int rc = fread(sync, 1, SYNC_MARKER_SIZE, file->file);
    if (rc != SYNC_MARKER_SIZE)
    {
        if (ferror(file->file))
        {
            MXS_ERROR("Failed to read file: %d %s", errno, mxs_strerror(errno));
        }
        else if (rc > 0 || !feof(file->file))
        {
            MXS_ERROR("Short read when reading sync marker. Read %d bytes instead of %d",
                      rc,
                      SYNC_MARKER_SIZE);
        }
        return false;
    }

    if (memcmp(file->sync, sync, SYNC_MARKER_SIZE))
    {
        long pos = ftell(file->file);
        long expected = file->data_start_pos + file->buffer_size + SYNC_MARKER_SIZE;
        if (pos != expected)
        {
            MXS_ERROR("Sync marker mismatch due to wrong file offset. file is at %ld "
                      "when it should be at %ld.",
                      pos,
                      expected);
        }
        else
        {
            MXS_ERROR("Sync marker mismatch.");
        }
        return false;
    }

    /** Increment block count */
    file->blocks_read++;
    file->bytes_read += file->buffer_size;
    return true;
}

static uint8_t* read_block_data(MAXAVRO_FILE* file, long deflate_size)
{
    uint8_t* temp_buffer = (uint8_t*)MXS_MALLOC(deflate_size);
    uint8_t* buffer = NULL;

    if (temp_buffer && fread(temp_buffer, 1, deflate_size, file->file) == (size_t)deflate_size)
    {
        unsigned long inflate_size = 0;

        switch (file->codec)
        {
        case MAXAVRO_CODEC_NULL:
            file->buffer_size = deflate_size;
            buffer = temp_buffer;
            temp_buffer = NULL;
            break;

        case MAXAVRO_CODEC_DEFLATE:
            inflate_size = deflate_size * 2;

            if ((buffer = (uint8_t*)MXS_MALLOC(inflate_size)))
            {
                z_stream stream;
                stream.avail_in = deflate_size;
                stream.next_in = temp_buffer;
                stream.avail_out = inflate_size;
                stream.next_out = buffer;
                stream.zalloc = 0;
                stream.zfree = 0;
                inflateInit2(&stream, -15);

                int rc;

                while ((rc = inflate(&stream, Z_FINISH)) == Z_BUF_ERROR)
                {
                    int increment = inflate_size;
                    uint8_t* temp = (uint8_t*)MXS_REALLOC(buffer, inflate_size + increment);

                    if (temp)
                    {
                        buffer = temp;
                        stream.avail_out += increment;
                        stream.next_out = buffer + stream.total_out;
                        inflate_size += increment;
                    }
                    else
                    {
                        break;
                    }
                }

                if (rc == Z_STREAM_END)
                {
                    file->buffer_size = stream.total_out;
                }
                else
                {
                    MXS_ERROR("Failed to inflate value: %s", zError(rc));
                    MXS_FREE(buffer);
                    buffer = NULL;
                }

                inflateEnd(&stream);
            }
            break;

        case MAXAVRO_CODEC_SNAPPY:
            // TODO: implement snappy compression
            break;

        default:
            break;
        }

        MXS_FREE(temp_buffer);
    }

    return buffer;
}

bool maxavro_read_datablock_start(MAXAVRO_FILE* file)
{
    /** The actual start of the binary block */
    file->block_start_pos = ftell(file->file);
    file->metadata_read = false;
    uint64_t records, bytes;
    bool rval = maxavro_read_integer_from_file(file, &records)
        && maxavro_read_integer_from_file(file, &bytes);

    if (rval)
    {
        rval = false;
        long pos = ftell(file->file);

        if (pos == -1)
        {
            MXS_ERROR("Failed to read datablock start: %d, %s",
                      errno,
                      mxs_strerror(errno));
        }
        else
        {
            MXS_FREE(file->buffer);
            file->buffer = read_block_data(file, bytes);

            if (file->buffer)
            {
                file->buffer_end = file->buffer + file->buffer_size;
                file->buffer_ptr = file->buffer;
                file->records_in_block = records;
                file->records_read_from_block = 0;
                file->data_start_pos = pos;
                mxb_assert(file->data_start_pos > file->block_start_pos);
                file->metadata_read = true;
                rval = maxavro_verify_block(file);
            }
        }
    }
    else if (maxavro_get_error(file) != MAXAVRO_ERR_NONE)
    {
        MXS_ERROR("Failed to read data block start.");
    }
    else if (feof(file->file))
    {
        clearerr(file->file);
    }
    return rval;
}

/** The header metadata is encoded as an Avro map with @c bytes encoded
 * key-value pairs. A @c bytes value is written as a length encoded string
 * where the length of the value is stored as a @c long followed by the
 * actual data. */
static char* read_schema(MAXAVRO_FILE* file)
{
    char* rval = NULL;
    MAXAVRO_MAP* head = maxavro_read_map_from_file(file);
    MAXAVRO_MAP* map = head;

    while (map)
    {
        if (strcmp(map->key, "avro.schema") == 0)
        {
            rval = strdup(map->value);
        }
        if (strcmp(map->key, "avro.codec") == 0)
        {
            if (strcmp(map->value, "null") == 0)
            {
                file->codec = MAXAVRO_CODEC_NULL;
            }
            else if (strcmp(map->value, "deflate") == 0)
            {
                file->codec = MAXAVRO_CODEC_DEFLATE;
            }
            else if (strcmp(map->value, "snappy") == 0)
            {
                file->codec = MAXAVRO_CODEC_SNAPPY;
            }
            else
            {
                MXS_ERROR("Unknown Avro codec: %s", map->value);
            }
        }
        map = map->next;
    }

    if (rval == NULL)
    {
        MXS_ERROR("No schema found from Avro header.");
    }

    maxavro_map_free(head);
    return rval;
}

/**
 * @brief Open an avro file
 *
 * This function performs checks on the file header and creates an internal
 * representation of the file's schema. This schema can be accessed for more
 * information about the fields.
 * @param filename File to open
 * @return Pointer to opened file or NULL if an error occurred
 */
MAXAVRO_FILE* maxavro_file_open(const char* filename)
{
    FILE* file = fopen(filename, "rb");
    if (!file)
    {
        MXS_ERROR("Failed to open file '%s': %d, %s", filename, errno, strerror(errno));
        return NULL;
    }

    char magic[AVRO_MAGIC_SIZE];

    if (fread(magic, 1, AVRO_MAGIC_SIZE, file) != AVRO_MAGIC_SIZE)
    {
        fclose(file);
        MXS_ERROR("Failed to read file magic marker from '%s'", filename);
        return NULL;
    }

    if (memcmp(magic, avro_magic, AVRO_MAGIC_SIZE) != 0)
    {
        fclose(file);
        MXS_ERROR("Error: Avro magic marker bytes are not correct.");
        return NULL;
    }

    bool error = false;

    MAXAVRO_FILE* avrofile = (MAXAVRO_FILE*)calloc(1, sizeof(MAXAVRO_FILE));
    char* my_filename = strdup(filename);

    if (avrofile && my_filename)
    {
        avrofile->file = file;
        avrofile->filename = my_filename;
        avrofile->last_error = MAXAVRO_ERR_NONE;

        char* schema = read_schema(avrofile);

        if (schema)
        {
            avrofile->schema = maxavro_schema_alloc(schema);

            if (avrofile->schema
                && maxavro_read_sync(file, avrofile->sync)
                && maxavro_read_datablock_start(avrofile))
            {
                avrofile->header_end_pos = avrofile->block_start_pos;
            }
            else
            {
                maxavro_schema_free(avrofile->schema);
                error = true;
            }
            MXS_FREE(schema);
        }
        else
        {
            error = true;
        }
    }
    else
    {
        error = true;
    }

    if (error)
    {
        fclose(file);
        MXS_FREE(avrofile);
        MXS_FREE(my_filename);
        avrofile = NULL;
    }

    return avrofile;
}

/**
 * @brief Return the last error from the file
 * @param file File to check
 * @return The last error or MAXAVRO_ERR_NONE if no errors have occurred
 */
enum maxavro_error maxavro_get_error(MAXAVRO_FILE* file)
{
    return file->last_error;
}

/**
 * @brief Get the error string for this file
 * @param file File to check
 * @return Error in string form
 */
const char* maxavro_get_error_string(MAXAVRO_FILE* file)
{
    switch (file->last_error)
    {
    case MAXAVRO_ERR_IO:
        return "MAXAVRO_ERR_IO";

    case MAXAVRO_ERR_MEMORY:
        return "MAXAVRO_ERR_MEMORY";

    case MAXAVRO_ERR_VALUE_OVERFLOW:
        return "MAXAVRO_ERR_VALUE_OVERFLOW";

    case MAXAVRO_ERR_NONE:
        return "MAXAVRO_ERR_NONE";

    default:
        return "UNKNOWN ERROR";
    }
}

/**
 * @brief Close an avro file
 * @param file File to close
 */
void maxavro_file_close(MAXAVRO_FILE* file)
{
    if (file)
    {
        fclose(file->file);
        MXS_FREE(file->buffer);
        MXS_FREE(file->filename);
        maxavro_schema_free(file->schema);
        MXS_FREE(file);
    }
}

/**
 * @brief Read binary Avro header
 *
 * This reads the binary format Avro header from an Avro file. The header is the
 * start of the Avro file so it also includes the Avro magic marker bytes.
 *
 * @param file File to read from
 * @return Binary header or NULL if an error occurred
 */
GWBUF* maxavro_file_binary_header(MAXAVRO_FILE* file)
{
    long pos = file->header_end_pos;
    GWBUF* rval = NULL;

    if (fseek(file->file, 0, SEEK_SET) == 0)
    {
        if ((rval = gwbuf_alloc(pos)))
        {
            if (fread(GWBUF_DATA(rval), 1, pos, file->file) != (size_t)pos)
            {
                if (ferror(file->file))
                {
                    MXS_ERROR("Failed to read binary header: %d, %s",
                              errno,
                              mxs_strerror(errno));
                }
                else if (feof(file->file))
                {
                    MXS_ERROR("Short read when reading binary header.");
                }
                else
                {
                    MXS_ERROR("Unspecified error when reading binary header.");
                }
                gwbuf_free(rval);
                rval = NULL;
            }
        }
        else
        {
            MXS_ERROR("Memory allocation failed when allocating %ld bytes.", pos);
        }
    }
    else
    {
        MXS_ERROR("Failed to read binary header: %d, %s",
                  errno,
                  mxs_strerror(errno));
    }

    return rval;
}
