/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../mysqlmon.h"

#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/utils.h>

#include <zlib.h>

/**
 * Crash-safe storage of server states
 *
 * This file contains functions to store and load backups of the server states.
 */

/** Schema version, backups must have a matching version */
#define MMB_SCHEMA_VERSION     1

/** Constants for byte lengths of the values */
#define MMB_LEN_BYTES          4
#define MMB_LEN_SCHEMA_VERSION 1
#define MMB_LEN_CRC32          4
#define MMB_LEN_VALUE_TYPE     1
#define MMB_LEN_SERVER_STATUS  4

/** Type of the stored value */
enum stored_value_type
{
    SVT_SERVER = 1, // Generic server state information
    SVT_MASTER = 2, // The master server name
};

/**
 * @brief Remove .tmp suffix and rename file
 *
 * @param src File to rename
 * @return True if file was successfully renamed
 */
static bool rename_tmp_file(const char *src)
{
    char dest[strlen(src) + 1];
    strcpy(dest, src);

    char *tail = strrchr(dest, '.');
    ss_dassert(tail && strcmp(tail, ".tmp") == 0);
    *tail = '\0';

    bool rval = true;

    if (rename(src, dest) == -1)
    {
        rval = false;
        MXS_ERROR("Failed to rename journal file '%s' to '%s': %d, %s",
                  src, dest, errno, mxs_strerror(errno));
    }

    return rval;
}

/**
 * @brief Open temporary file
 *
 * @param monitor Monitor
 * @param path Output where the path is stored
 * @return Opened file or NULL on error
 */
static FILE* open_tmp_file(MXS_MONITOR *monitor, char *path)
{
    const char *name_template = "%s/%s/";
    const char filename[] = "mysqlmon.dat.tmp";
    int nbytes = snprintf(path, PATH_MAX, name_template, get_datadir(), monitor->name);

    FILE *rval = NULL;

    if (nbytes < PATH_MAX - sizeof(filename) && mxs_mkdir_all(path, 0744))
    {
        strcat(path, filename);

        if ((rval = fopen(path, "wb")) == NULL)
        {
            MXS_ERROR("Failed to open file '%s': %d, %s", path, errno, mxs_strerror(errno));
        }
    }

    return rval;
}

/**
 * @brief Store server data to in-memory buffer
 *
 * @param monitor Monitor
 * @param data Pointer to in-memory buffer used for storage, should be at least
 *             PATH_MAX bytes long
 * @param size Size of @c data
 */
static void store_data(MXS_MONITOR *monitor, char *data, uint32_t size)
{
    MYSQL_MONITOR* handle = (MYSQL_MONITOR*) monitor->handle;
    char *ptr = data;

    /** Store the data length */
    ss_dassert(sizeof(size) == MMB_LEN_BYTES);
    *ptr++ = size;
    *ptr++ = (size >> 8);
    *ptr++ = (size >> 16);
    *ptr++ = (size >> 24);

    /** Then the schema version */
    *ptr++ = MMB_SCHEMA_VERSION;

    /** Store the states of all servers */
    for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
    {
        *ptr++ = (char)SVT_SERVER; // Value type
        strcpy(ptr, db->server->unique_name); // Name of the server
        ptr += strlen(db->server->unique_name) + 1;

        uint32_t status = db->server->status; // Server status as 4 byte integer
        ss_dassert(sizeof(status) == MMB_LEN_SERVER_STATUS);
        *ptr++ = status;
        *ptr++ = (status >> 8);
        *ptr++ = (status >> 16);
        *ptr++ = (status >> 24);
    }

    /** Store the current root master if we have one */
    if (handle->master)
    {
        *ptr++ = (char)SVT_MASTER;
        strcpy(ptr, handle->master->server->unique_name);
        ptr += strlen(handle->master->server->unique_name) + 1;
    }

    /** Calculate the CRC32 for the complete payload minus the CRC32 bytes */
    uint32_t crc = crc32(0L, NULL, 0);
    crc = crc32(crc, (uint8_t*)data + MMB_LEN_BYTES, size - MMB_LEN_CRC32);
    ss_dassert(sizeof(crc) == MMB_LEN_CRC32);

    *ptr++ = crc;
    *ptr++ = (crc >> 8);
    *ptr++ = (crc >> 16);
    *ptr++ = (crc >> 24);

    ss_dassert(ptr - data == size + MMB_LEN_BYTES);
}

static int get_data_file_path(MXS_MONITOR *monitor, char *path)
{
    const char *name_template = "%s/%s/mysqlmon.dat";
    return snprintf(path, PATH_MAX, name_template, get_datadir(), monitor->name);
}

/**
 * @brief Open stored backup file
 *
 * @param monitor Monitor to reload
 * @param path Output where path is stored
 * @return Opened file or NULL on error
 */
static FILE* open_data_file(MXS_MONITOR *monitor, char *path)
{
    FILE *rval = NULL;
    int nbytes = get_data_file_path(monitor, path);

    if (nbytes < PATH_MAX)
    {
        if ((rval = fopen(path, "rb")) == NULL && errno != ENOENT)
        {
            MXS_ERROR("Failed to open journal file: %d, %s", errno, mxs_strerror(errno));
        }
    }

    return rval;
}

/**
 * Check that memory area contains a null terminator
 */
static bool has_null_terminator(const char *data, const char *end)
{
    while (data < end)
    {
        if (*data == '\0')
        {
            return true;
        }
        data++;
    }

    return false;
}

/**
 * Process a generic server
 */
static const char* process_server(MXS_MONITOR *monitor, const char *data, const char *end)
{
    for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
    {
        if (strcmp(db->server->unique_name, data) == 0)
        {
            const unsigned char *sptr = (unsigned char*)strchr(data, '\0');
            ss_dassert(sptr);
            sptr++;

            uint32_t state = sptr[0] | (sptr[1] << 8) | (sptr[2] << 16) | (sptr[3] << 24);
            server_set_status_nolock(db->server, state);
            monitor_set_pending_status(db, state);
            break;
        }
    }

    data += strlen(data) + 1 + MMB_LEN_SERVER_STATUS;

    return data;
}

/**
 * Process a master
 */
static const char* process_master(MXS_MONITOR *monitor, const char *data, const char *end)
{
    for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
    {
        if (strcmp(db->server->unique_name, data) == 0)
        {
            MYSQL_MONITOR* handle = (MYSQL_MONITOR*)monitor->handle;
            handle->master = db;
            break;
        }
    }

    data += strlen(data) + 1;

    return data;
}

/**
 * Check that the calculated CRC32 matches the one stored on disk
 */
static bool check_crc32(const uint8_t *data, uint32_t size, const uint8_t *crc_ptr)
{
    uint32_t crc = crc_ptr[0] | (crc_ptr[1] << 8) | (crc_ptr[2] << 16) | (crc_ptr[3] << 24);
    uint32_t calculated_crc = crc32(0L, NULL, 0);
    calculated_crc = crc32(calculated_crc, data, size);
    return calculated_crc == crc;
}

/**
 * Process the stored backup data
 */
static bool process_data_file(MXS_MONITOR *monitor, const char *data, const char *crc_ptr)
{
    const char *ptr = data;
    ss_debug(const char *prevptr = ptr);

    while (ptr < crc_ptr)
    {
        /** All values contain a null terminated string */
        if (!has_null_terminator(ptr, crc_ptr))
        {
            MXS_ERROR("Possible corrupted journal file (no null terminator found). Ignoring.");
            return false;
        }

        enum stored_value_type type = *ptr;
        ptr += MMB_LEN_VALUE_TYPE;

        switch (type)
        {
        case SVT_SERVER:
            ptr = process_server(monitor, ptr, crc_ptr);
            break;

        case SVT_MASTER:
            ptr = process_master(monitor, ptr, crc_ptr);
            break;

        default:
            MXS_ERROR("Possible corrupted journal file (unknown stored value). Ignoring.");
            return false;
        }
        ss_dassert(prevptr != ptr);
        ss_debug(prevptr = ptr);
    }

    ss_dassert(ptr == crc_ptr);
    return true;
}

void store_server_backup(MXS_MONITOR *monitor)
{
    /** Calculate how much memory we need to allocate */
    uint32_t size = MMB_LEN_SCHEMA_VERSION + MMB_LEN_CRC32;

    for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
    {
        /** Each server is stored as a type byte and a null-terminated string
         * followed by eight byte server status. */
        size += MMB_LEN_VALUE_TYPE + strlen(db->server->unique_name) + 1 + MMB_LEN_SERVER_STATUS;
    }

    MYSQL_MONITOR* handle = (MYSQL_MONITOR*) monitor->handle;

    if (handle->master)
    {
        /** The master server name is stored as a null terminated string */
        size += MMB_LEN_VALUE_TYPE + strlen(handle->master->server->unique_name) + 1;
    }

    /** 4 bytes for file length, 1 byte for schema version and 4 bytes for CRC32 */
    uint32_t buffer_size = size + MMB_LEN_BYTES;
    char *data = (char*)MXS_MALLOC(buffer_size);
    char path[PATH_MAX + 1];

    if (data)
    {
        /** Store the data in memory first */
        store_data(monitor, data, size);

        FILE *file = open_tmp_file(monitor, path);

        if (file)
        {
            /** Write the data to a temp file and rename it to the final name */
            if (fwrite(data, 1, buffer_size, file) == buffer_size && fflush(file) == 0)
            {
                if (!rename_tmp_file(path))
                {
                    unlink(path);
                }
            }
            else
            {
                MXS_ERROR("Failed to write journal data to disk: %d, %s",
                          errno, mxs_strerror(errno));
            }
            fclose(file);
        }
    }
    MXS_FREE(data);
}

void load_server_backup(MXS_MONITOR *monitor)
{
    char path[PATH_MAX];
    FILE *file = open_data_file(monitor, path);

    if (file)
    {
        uint32_t size = 0;
        size_t bytes = fread(&size, 1, MMB_LEN_BYTES, file);
        ss_dassert(sizeof(size) == MMB_LEN_BYTES);

        if (bytes == MMB_LEN_BYTES)
        {
            /** Payload contents:
             *
             * - One byte of schema version
             * - `size - 5` bytes of data
             * - Trailing 4 bytes of CRC32
             */
            char *data = (char*)MXS_MALLOC(size);

            if (data && (bytes = fread(data, 1, size, file)) == size)
            {
                if (*data == MMB_SCHEMA_VERSION)
                {
                    if (check_crc32((uint8_t*)data, size - MMB_LEN_CRC32,
                                    (uint8_t*)data + size - MMB_LEN_CRC32))
                    {
                        if (process_data_file(monitor, data + MMB_LEN_SCHEMA_VERSION,
                                              data + size - MMB_LEN_CRC32))
                        {
                            MXS_NOTICE("Loaded server states from journal file: %s", path);
                        }
                    }
                    else
                    {
                        MXS_ERROR("CRC32 mismatch in journal file. Ignoring.");
                    }
                }
                else
                {
                    MXS_ERROR("Unknown journal schema version: %d", (int)*data);
                }
            }
            else if (data)
            {
                if (ferror(file))
                {
                    MXS_ERROR("Failed to read journal file: %d, %s", errno, mxs_strerror(errno));
                }
                else
                {
                    MXS_ERROR("Failed to read journal file: Expected %u bytes, "
                        "read %lu bytes.", size, bytes);
                }
            }
            MXS_FREE(data);
        }
        else
        {
            if (ferror(file))
            {
                MXS_ERROR("Failed to read journal file length: %d, %s",
                          errno, mxs_strerror(errno));
            }
            else
            {
                MXS_ERROR("Failed to read journal file length: Expected %d bytes, "
                          "read %lu bytes.", MMB_LEN_BYTES, bytes);
            }
        }

        fclose(file);
    }
}

void remove_server_backup(MXS_MONITOR *monitor)
{
    char path[PATH_MAX];

    if (get_data_file_path(monitor, path) < PATH_MAX)
    {
        unlink(path);
    }
    else
    {
        MXS_ERROR("Path to monitor journal directory is too long.");
    }
}
