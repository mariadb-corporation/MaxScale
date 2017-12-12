/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file avro_file.c - File operations for the Avro router
 *
 * This file contains functions that handle the low level file operations for
 * the Avro router. The handling of Avro data files is done via the Avro C API
 * but the handling of MySQL format binary logs is done manually.
 *
 * Parts of this file have been copied from blr_file.c and modified for other
 * uses.
 */

#include "avrorouter.h"

#include <binlog_common.h>
#include <blr_constants.h>
#include <glob.h>
#include <ini.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/pcre2.h>
#include <maxscale/utils.h>

static const char *statefile_section = "avro-conversion";
static const char *ddl_list_name = "table-ddl.list";
void handle_query_event(AVRO_INSTANCE *router, REP_HEADER *hdr,
                        int *pending_transaction, uint8_t *ptr);
bool is_create_table_statement(AVRO_INSTANCE *router, char* ptr, size_t len);
void avro_notify_client(AVRO_CLIENT *client);
void avro_update_index(AVRO_INSTANCE* router);
void update_used_tables(AVRO_INSTANCE* router);
TABLE_CREATE* table_create_from_schema(const char* file, const char* db,
                                       const char* table, int version);

/**
 * Open a binlog file for reading
 *
 * @param router    The router instance
 * @param file      The binlog file name
 */
bool avro_open_binlog(const char *binlogdir, const char *file, int *dest)
{
    char path[PATH_MAX + 1] = "";
    int fd;

    snprintf(path, sizeof(path), "%s/%s", binlogdir, file);

    if ((fd = open(path, O_RDONLY)) == -1)
    {
        if (errno != ENOENT)
        {
            MXS_ERROR("Failed to open binlog file %s: %d, %s", path, errno,
                      mxs_strerror(errno));
        }
        return false;
    }

    if (lseek(fd, BINLOG_MAGIC_SIZE, SEEK_SET) < 4)
    {
        /* If for any reason the file's length is between 1 and 3 bytes
         * then report an error. */
        MXS_ERROR("Binlog file %s has an invalid length.", path);
        close(fd);
        return false;
    }

    *dest = fd;
    return true;
}

/**
 * Close a binlog file
 * @param fd Binlog file descriptor
 */
void avro_close_binlog(int fd)
{
    close(fd);
}

/**
 * @brief Allocate an Avro table
 *
 * Create an Aro table and prepare it for writing.
 * @param filepath Path to the created file
 * @param json_schema The schema of the table in JSON format
 */
AVRO_TABLE* avro_table_alloc(const char* filepath, const char* json_schema, const char *codec,
                             size_t block_size)
{
    AVRO_TABLE *table = MXS_CALLOC(1, sizeof(AVRO_TABLE));
    if (table)
    {
        if (avro_schema_from_json_length(json_schema, strlen(json_schema),
                                         &table->avro_schema))
        {
            MXS_ERROR("Avro error: %s", avro_strerror());
            MXS_INFO("Avro schema: %s", json_schema);
            MXS_FREE(table);
            return NULL;
        }

        int rc = 0;

        if (access(filepath, F_OK) == 0)
        {
            rc = avro_file_writer_open_bs(filepath, &table->avro_file, block_size);
        }
        else
        {
            rc = avro_file_writer_create_with_codec(filepath, table->avro_schema,
                                                    &table->avro_file, codec, block_size);
        }

        if (rc)
        {
            MXS_ERROR("Avro error: %s", avro_strerror());
            avro_schema_decref(table->avro_schema);
            MXS_FREE(table);
            return NULL;
        }

        if ((table->avro_writer_iface = avro_generic_class_from_schema(table->avro_schema)) == NULL)
        {
            MXS_ERROR("Avro error: %s", avro_strerror());
            avro_schema_decref(table->avro_schema);
            avro_file_writer_close(table->avro_file);
            MXS_FREE(table);
            return NULL;
        }

        table->json_schema = MXS_STRDUP_A(json_schema);
        table->filename = MXS_STRDUP_A(filepath);
    }
    return table;
}

/**
 * @brief Write a new ini file with current conversion status
 *
 * The file is stored in the cache directory as 'avro-conversion.ini'.
 * @param router Avro router instance
 * @return True if the file was written successfully to disk
 *
 */
bool avro_save_conversion_state(AVRO_INSTANCE *router)
{
    FILE *config_file;
    char filename[PATH_MAX + 1];

    snprintf(filename, sizeof(filename), "%s/"AVRO_PROGRESS_FILE".tmp", router->avrodir);

    /* open file for writing */
    config_file = fopen(filename, "wb");

    if (config_file == NULL)
    {
        MXS_ERROR("Failed to open file '%s': %d, %s", filename,
                  errno, mxs_strerror(errno));
        return false;
    }

    fprintf(config_file, "[%s]\n", statefile_section);
    fprintf(config_file, "position=%lu\n", router->current_pos);
    fprintf(config_file, "gtid=%lu-%lu-%lu:%lu\n", router->gtid.domain,
            router->gtid.server_id, router->gtid.seq, router->gtid.event_num);
    fprintf(config_file, "file=%s\n", router->binlog_name);
    fclose(config_file);

    /* rename tmp file to right filename */
    char newname[PATH_MAX + 1];
    snprintf(newname, sizeof(newname), "%s/"AVRO_PROGRESS_FILE, router->avrodir);
    int rc = rename(filename, newname);

    if (rc == -1)
    {
        MXS_ERROR("Failed to rename file '%s' to '%s': %d, %s", filename, newname,
                  errno, mxs_strerror(errno));
        return false;
    }

    return true;
}

/**
 * @brief Callback for the @c ini_parse of the stored conversion position
 *
 * @param data User provided data
 * @param section Section name
 * @param key Parameter name
 * @param value Parameter value
 * @return 1 if the parsing should continue, 0 if an error was detected
 */
static int conv_state_handler(void* data, const char* section, const char* key, const char* value)
{
    AVRO_INSTANCE *router = (AVRO_INSTANCE*) data;

    if (strcmp(section, statefile_section) == 0)
    {
        if (strcmp(key, "gtid") == 0)
        {
            char tempval[strlen(value) + 1];
            memcpy(tempval, value, sizeof(tempval));
            char *saved, *domain = strtok_r(tempval, ":-\n", &saved);
            char *serv_id = strtok_r(NULL, ":-\n", &saved);
            char *seq = strtok_r(NULL, ":-\n", &saved);
            char *subseq = strtok_r(NULL, ":-\n", &saved);

            if (domain && serv_id && seq && subseq)
            {
                router->gtid.domain = strtol(domain, NULL, 10);
                router->gtid.server_id = strtol(serv_id, NULL, 10);
                router->gtid.seq = strtol(seq, NULL, 10);
                router->gtid.event_num = strtol(subseq, NULL, 10);
            }
        }
        else if (strcmp(key, "position") == 0)
        {
            router->current_pos = strtol(value, NULL, 10);
        }
        else if (strcmp(key, "file") == 0)
        {
            size_t len = strlen(value);

            if (len > BINLOG_FNAMELEN)
            {
                MXS_ERROR("Provided value %s for key 'file' is too long. "
                          "The maximum allowed length is %d.", value, BINLOG_FNAMELEN);
                return 0;
            }

            strcpy(router->binlog_name, value);
        }
        else
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Load a stored conversion state from file
 *
 * @param router Avro router instance
 * @return True if the stored state was loaded successfully
 */
bool avro_load_conversion_state(AVRO_INSTANCE *router)
{
    char filename[PATH_MAX + 1];
    bool rval = false;

    snprintf(filename, sizeof(filename), "%s/"AVRO_PROGRESS_FILE, router->avrodir);

    /** No stored state, this is the first time the router is started */
    if (access(filename, F_OK) == -1)
    {
        return true;
    }

    MXS_NOTICE("[%s] Loading stored conversion state: %s", router->service->name, filename);

    int rc = ini_parse(filename, conv_state_handler, router);

    switch (rc)
    {
    case 0:
        rval = true;
        MXS_NOTICE("Loaded stored binary log conversion state: File: [%s] Position: [%ld] GTID: [%lu-%lu-%lu:%lu]",
                   router->binlog_name, router->current_pos, router->gtid.domain,
                   router->gtid.server_id, router->gtid.seq, router->gtid.event_num);
        break;

    case -1:
        MXS_ERROR("Failed to open file '%s'. ", filename);
        break;

    case -2:
        MXS_ERROR("Failed to allocate enough memory when parsing file '%s'. ", filename);
        break;

    default:
        MXS_ERROR("Failed to parse stored conversion state '%s', error "
                  "on line %d. ", filename, rc);
        break;
    }

    return rval;
}

/**
 * @brief Free an AVRO_TABLE
 *
 * @param table Table to free
 */
void avro_table_free(AVRO_TABLE *table)
{
    if (table)
    {
        avro_file_writer_flush(table->avro_file);
        avro_file_writer_close(table->avro_file);
        avro_value_iface_decref(table->avro_writer_iface);
        avro_schema_decref(table->avro_schema);
        MXS_FREE(table->json_schema);
        MXS_FREE(table->filename);
    }
}

/**
 * @brief Rotate to next file if it exists
 *
 * @param router Avro router instance
 * @param pos Current position, used for logging
 * @param stop_seen If a stop event was seen when processing current file
 * @return AVRO_OK if the next file exists, AVRO_LAST_FILE if this is the last
 * available file.
 */
static avro_binlog_end_t rotate_to_next_file_if_exists(AVRO_INSTANCE* router, uint64_t pos, bool stop_seen)
{
    avro_binlog_end_t rval = AVRO_LAST_FILE;

    if (binlog_next_file_exists(router->binlogdir, router->binlog_name))
    {
        char next_binlog[BINLOG_FNAMELEN + 1];
        if (snprintf(next_binlog, sizeof(next_binlog),
                     BINLOG_NAMEFMT, router->fileroot,
                     blr_file_get_next_binlogname(router->binlog_name)) >= sizeof(next_binlog))
        {
            MXS_ERROR("Next binlog name did not fit into the allocated buffer "
                      "but was truncated, aborting: %s", next_binlog);
            rval = AVRO_BINLOG_ERROR;
        }
        else
        {
            if (stop_seen)
            {
                MXS_NOTICE("End of binlog file [%s] at %lu with a "
                           "close event. Rotating to next binlog file [%s].",
                           router->binlog_name, pos, next_binlog);
            }
            else
            {
                MXS_NOTICE("End of binlog file [%s] at %lu with no "
                           "close or rotate event. Rotating to next binlog file [%s].",
                           router->binlog_name, pos, next_binlog);
            }

            rval = AVRO_OK;
            strcpy(router->binlog_name, next_binlog);
            router->binlog_position = 4;
            router->current_pos = 4;
        }
    }
    else if (stop_seen)
    {
        MXS_NOTICE("End of binlog file [%s] at %lu with a close event. "
                   "Next binlog file does not exist, pausing file conversion.",
                   router->binlog_name, pos);
    }

    return rval;
}

/**
 * @brief Rotate to a specific file
 *
 * This rotates the current binlog file being processed to a specific file.
 * Currently this is only used to rotate to files that rotate events point to.
 * @param router Avro router instance
 * @param pos Current position, only used for logging
 * @param next_binlog The next file to rotate to
 */
static void rotate_to_file(AVRO_INSTANCE* router, uint64_t pos, const char *next_binlog)
{
    /** Binlog file is processed, prepare for next one */
    MXS_NOTICE("End of binlog file [%s] at %lu. Rotating to file [%s].",
               router->binlog_name, pos, next_binlog);
    strcpy(router->binlog_name, next_binlog); // next_binlog is as big as router->binlog_name.
    router->binlog_position = 4;
    router->current_pos = 4;
}

/**
 * @brief Read the replication event payload
 *
 * @param router Avro router instance
 * @param hdr Replication header
 * @param pos Starting position of the event header
 * @return The event data or NULL if an error occurred
 */
static GWBUF* read_event_data(AVRO_INSTANCE *router, REP_HEADER* hdr, uint64_t pos)
{
    GWBUF* result;
    /* Allocate a GWBUF for the event */
    if ((result = gwbuf_alloc(hdr->event_size - BINLOG_EVENT_HDR_LEN + 1)))
    {
        uint8_t *data = GWBUF_DATA(result);
        int n = pread(router->binlog_fd, data, hdr->event_size - BINLOG_EVENT_HDR_LEN,
                      pos + BINLOG_EVENT_HDR_LEN);
        /** NULL-terminate for QUERY_EVENT processing */
        data[hdr->event_size - BINLOG_EVENT_HDR_LEN] = '\0';

        if (n != hdr->event_size - BINLOG_EVENT_HDR_LEN)
        {
            if (n == -1)
            {
                MXS_ERROR("Error reading the event at %lu in %s. "
                          "%s, expected %d bytes.",
                          pos, router->binlog_name,
                          mxs_strerror(errno),
                          hdr->event_size - BINLOG_EVENT_HDR_LEN);
            }
            else
            {
                MXS_ERROR("Short read when reading the event at %lu in %s. "
                          "Expected %d bytes got %d bytes.",
                          pos, router->binlog_name,
                          hdr->event_size - BINLOG_EVENT_HDR_LEN, n);
            }
            gwbuf_free(result);
            result = NULL;
        }
    }
    else
    {
        MXS_ERROR("Failed to allocate memory for binlog entry, "
                  "size %d at %lu.",
                  hdr->event_size, pos);
    }
    return result;
}

void notify_all_clients(AVRO_INSTANCE *router)
{
    AVRO_CLIENT *client = router->clients;
    int notified = 0;

    while (client)
    {
        spinlock_acquire(&client->catch_lock);
        if (client->cstate & AVRO_WAIT_DATA)
        {
            notified++;
            avro_notify_client(client);
        }
        spinlock_release(&client->catch_lock);

        client = client->next;
    }

    if (notified > 0)
    {
        MXS_INFO("Notified %d clients about new data.", notified);
    }
}

void do_checkpoint(AVRO_INSTANCE *router, uint64_t *total_rows, uint64_t *total_commits)
{
    update_used_tables(router);
    avro_flush_all_tables(router, AVROROUTER_FLUSH);
    avro_save_conversion_state(router);
    notify_all_clients(router);
    *total_rows += router->row_count;
    *total_commits += router->trx_count;
    router->row_count = router->trx_count = 0;
}

/**
 * @brief Read all replication events from a binlog file.
 *
 * Routine detects errors and pending transactions
 *
 * @param router        The router instance
 * @param fix           Whether to fix or not errors
 * @param debug         Whether to enable or not the debug for events
 * @return              How the binlog was closed
 * @see enum avro_binlog_end
 */
avro_binlog_end_t avro_read_all_events(AVRO_INSTANCE *router)
{
    uint8_t hdbuf[BINLOG_EVENT_HDR_LEN];
    unsigned long long pos = router->current_pos;
    unsigned long long last_known_commit = 4;
    char next_binlog[BINLOG_FNAMELEN + 1];
    REP_HEADER hdr;
    int pending_transaction = 0;
    uint8_t *ptr;
    uint64_t total_commits = 0, total_rows = 0;
    bool found_chksum = false;
    bool rotate_seen = false;
    bool stop_seen = false;

    if (router->binlog_fd == -1)
    {
        MXS_ERROR("Current binlog file %s is not open", router->binlog_name);
        return AVRO_BINLOG_ERROR;
    }

    while (!router->service->svc_do_shutdown)
    {
        int n;
        /* Read the header information from the file */
        if ((n = pread(router->binlog_fd, hdbuf, BINLOG_EVENT_HDR_LEN, pos)) != BINLOG_EVENT_HDR_LEN)
        {
            switch (n)
            {
            case 0:
                break;
            case -1:
                {
                    MXS_ERROR("Failed to read binlog file %s at position %llu (%s).",
                              router->binlog_name, pos,
                              mxs_strerror(errno));

                    if (errno == EBADF)
                        MXS_ERROR("Bad file descriptor in read binlog for file %s"
                                  ", descriptor %d.",
                                  router->binlog_name, router->binlog_fd);
                    break;
                }
            default:
                MXS_ERROR("Short read when reading the header. "
                          "Expected 19 bytes but got %d bytes. "
                          "Binlog file is %s, position %llu",
                          n, router->binlog_name, pos);
                break;
            }

            router->current_pos = pos;

            if (pending_transaction > 0)
            {
                MXS_ERROR("Binlog '%s' ends at position %lu and has an incomplete transaction at %lu. "
                          "Stopping file conversion.", router->binlog_name,
                          router->current_pos, router->binlog_position);
                return AVRO_OPEN_TRANSACTION;
            }
            else
            {
                /* any error */
                if (n != 0)
                {
                    return AVRO_BINLOG_ERROR;
                }
                else
                {
                    do_checkpoint(router, &total_rows, &total_commits);

                    MXS_INFO("Processed %lu transactions and %lu row events.",
                             total_commits, total_rows);
                    if (rotate_seen)
                    {
                        rotate_to_file(router, pos, next_binlog);
                        return AVRO_OK;
                    }
                    else
                    {
                        return rotate_to_next_file_if_exists(router, pos, stop_seen);
                    }
                }
            }
        }

        /* fill replication header struct */
        hdr.timestamp = EXTRACT32(hdbuf);
        hdr.event_type = hdbuf[4];
        hdr.serverid = EXTRACT32(&hdbuf[5]);
        hdr.event_size = extract_field(&hdbuf[9], 32);
        hdr.next_pos = EXTRACT32(&hdbuf[13]);
        hdr.flags = EXTRACT16(&hdbuf[17]);

        /* Check event type against MAX_EVENT_TYPE */

        if (hdr.event_type > MAX_EVENT_TYPE_MARIADB10)
        {
            MXS_ERROR("Invalid MariaDB 10 event type 0x%x. "
                      "Binlog file is %s, position %llu",
                      hdr.event_type, router->binlog_name, pos);
            router->binlog_position = last_known_commit;
            router->current_pos = pos;
            return AVRO_BINLOG_ERROR;
        }

        if (hdr.event_size <= 0)
        {
            MXS_ERROR("Event size error: "
                      "size %d at %llu.",
                      hdr.event_size, pos);

            router->binlog_position = last_known_commit;
            router->current_pos = pos;
            return AVRO_BINLOG_ERROR;
        }

        GWBUF *result = read_event_data(router, &hdr, pos);

        if (result == NULL)
        {
            router->binlog_position = last_known_commit;
            router->current_pos = pos;
            MXS_WARNING("an error has been found. "
                        "Setting safe pos to %lu, current pos %lu",
                        router->binlog_position, router->current_pos);
            return AVRO_BINLOG_ERROR;
        }

        /* check for pending transaction */
        if (pending_transaction == 0)
        {
            last_known_commit = pos;
        }

        /* get event content */
        ptr = GWBUF_DATA(result);

        MXS_DEBUG("%s(%x) - %llu", binlog_event_name(hdr.event_type), hdr.event_type, pos);

        uint32_t original_size = hdr.event_size;

        if (router->binlog_checksum)
        {
            hdr.event_size -= 4;
        }

        /* check for FORMAT DESCRIPTION EVENT */
        if (hdr.event_type == FORMAT_DESCRIPTION_EVENT)
        {
            const int BLRM_FDE_EVENT_TYPES_OFFSET = 2 + 50 + 4 + 1;
            const int FDE_EXTRA_BYTES = 5;
            int event_header_length = ptr[BLRM_FDE_EVENT_TYPES_OFFSET - 1];
            int n_events = hdr.event_size - event_header_length - BLRM_FDE_EVENT_TYPES_OFFSET - FDE_EXTRA_BYTES;
            uint8_t* checksum = ptr + hdr.event_size - event_header_length - FDE_EXTRA_BYTES;

            router->event_types = n_events;
            router->binlog_checksum = checksum[0];
        }
        /* Decode CLOSE/STOP Event */
        else if (hdr.event_type == STOP_EVENT)
        {
            char next_file[BLRM_BINLOG_NAME_STR_LEN + 1];
            stop_seen = true;
            snprintf(next_file, sizeof(next_file), BINLOG_NAMEFMT, router->fileroot,
                     blr_file_get_next_binlogname(router->binlog_name));
        }
        else if (hdr.event_type == MARIADB_ANNOTATE_ROWS_EVENT)
        {
            MXS_INFO("Annotate_rows_event: %.*s", hdr.event_size - BINLOG_EVENT_HDR_LEN, ptr);
            pos += original_size;
            router->current_pos = pos;
            continue;
        }
        else if (hdr.event_type == TABLE_MAP_EVENT)
        {
            handle_table_map_event(router, &hdr, ptr);
        }
        else if ((hdr.event_type >= WRITE_ROWS_EVENTv0 && hdr.event_type <= DELETE_ROWS_EVENTv1) ||
                 (hdr.event_type >= WRITE_ROWS_EVENTv2 && hdr.event_type <= DELETE_ROWS_EVENTv2))
        {
            router->row_count++;
            handle_row_event(router, &hdr, ptr);
        }
        /* Decode ROTATE EVENT */
        else if (hdr.event_type == ROTATE_EVENT)
        {
            int len = hdr.event_size - BINLOG_EVENT_HDR_LEN - 8;

            if (found_chksum)
            {
                len -= 4;
            }

            if (len > BINLOG_FNAMELEN)
            {
                MXS_WARNING("Truncated binlog name from %d to %d characters.",
                            len, BINLOG_FNAMELEN);
                len = BINLOG_FNAMELEN;
            }

            memcpy(next_binlog, ptr + 8, len);
            next_binlog[len] = 0;
            rotate_seen = true;

        }
        else if (hdr.event_type == MARIADB10_GTID_EVENT)
        {
            uint64_t n_sequence; /* 8 bytes */
            uint32_t domainid; /* 4 bytes */
            unsigned int flags; /* 1 byte */
            n_sequence = extract_field(ptr, 64);
            domainid = extract_field(ptr + 8, 32);
            flags = *(ptr + 8 + 4);
            router->gtid.domain = domainid;
            router->gtid.server_id = hdr.serverid;
            router->gtid.seq = n_sequence;
            router->gtid.event_num = 0;
            router->gtid.timestamp = hdr.timestamp;

            /* GTID event flags check, for 10.0 and 10.1 */
            if ((flags & (MARIADB_FL_DDL | MARIADB_FL_STANDALONE)) == 0)
            {
                pending_transaction = 1;
            }
        }
        /**
         * Check QUERY_EVENT
         *
         * Check for BEGIN ( ONLY for mysql 5.6, mariadb 5.5 )
         * Check for COMMIT (not transactional engines)
         */
        else if (hdr.event_type == QUERY_EVENT)
        {
            int trx_before = pending_transaction;
            handle_query_event(router, &hdr, &pending_transaction, ptr);

            if (trx_before != pending_transaction)
            {
                /** A non-transactional engine finished a transaction */
                router->trx_count++;
            }
        }
        else if (hdr.event_type == XID_EVENT)
        {
            router->trx_count++;
            pending_transaction = 0;

            if (router->row_count >= router->row_target ||
                router->trx_count >= router->trx_target)
            {
                do_checkpoint(router, &total_rows, &total_commits);
            }
        }

        gwbuf_free(result);

        /* pos and next_pos sanity checks */
        if (hdr.next_pos > 0 && hdr.next_pos < pos)
        {
            MXS_INFO("Binlog %s: next pos %u < pos %llu, truncating to %llu",
                     router->binlog_name, hdr.next_pos, pos, pos);
            break;
        }

        if (hdr.next_pos > 0 && hdr.next_pos != (pos + original_size))
        {
            MXS_INFO("Binlog %s: next pos %u != (pos %llu + event_size %u), truncating to %llu",
                     router->binlog_name, hdr.next_pos, pos, hdr.event_size, pos);
            break;
        }

        /* set pos to new value */
        if (hdr.next_pos > 0)
        {
            pos = hdr.next_pos;
            router->current_pos = pos;
        }
        else
        {
            MXS_ERROR("Current event type %d @ %llu has nex pos = %u : exiting",
                      hdr.event_type, pos, hdr.next_pos);
            break;
        }
    }

    return AVRO_BINLOG_ERROR;
}

/**
 * Read the field names from the stored Avro schemas
 *
 * @param router Router instance
 */
void avro_load_metadata_from_schemas(AVRO_INSTANCE *router)
{
    char path[PATH_MAX + 1];
    snprintf(path, sizeof(path), "%s/*.avsc", router->avrodir);
    glob_t files;

    if (glob(path, 0, NULL, &files) != GLOB_NOMATCH)
    {
        char db[MYSQL_DATABASE_MAXLEN + 1], table[MYSQL_TABLE_MAXLEN + 1];
        char table_ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2];
        int version = 0;

        /** Glob sorts the files in ascending order which means that processing
         * them in reverse should give us the newest schema first. */
        for (int i = files.gl_pathc - 1; i > -1; i--)
        {
            char *dbstart = strrchr(files.gl_pathv[i], '/');
            ss_dassert(dbstart);
            dbstart++;

            char *tablestart = strchr(dbstart, '.');
            ss_dassert(tablestart);

            snprintf(db, sizeof(db), "%.*s", (int)(tablestart - dbstart), dbstart);
            tablestart++;

            char *versionstart = strchr(tablestart, '.');
            ss_dassert(versionstart);

            snprintf(table, sizeof(table), "%.*s", (int)(versionstart - tablestart), tablestart);
            versionstart++;

            char *suffix = strchr(versionstart, '.');
            char *versionend = NULL;
            version = strtol(versionstart, &versionend, 10);

            if (versionend == suffix)
            {
                snprintf(table_ident, sizeof(table_ident), "%s.%s", db, table);
                TABLE_CREATE *old = hashtable_fetch(router->created_tables, table_ident);

                if (old == NULL || version > old->version)
                {
                    TABLE_CREATE *created = table_create_from_schema(files.gl_pathv[i],
                                                                     db, table, version);
                    if (old)
                    {
                        hashtable_delete(router->created_tables, table_ident);
                    }
                    hashtable_add(router->created_tables, table_ident, created);
                }
            }
            else
            {
                MXS_ERROR("Malformed schema file name: %s", files.gl_pathv[i]);
            }
        }
    }

    globfree(&files);
}

/**
 * @brief Flush all Avro records to disk
 * @param router Avro router instance
 */
void avro_flush_all_tables(AVRO_INSTANCE *router, enum avrorouter_file_op flush)
{
    HASHITERATOR *iter = hashtable_iterator(router->open_tables);

    if (iter)
    {
        char *key;
        while ((key = (char*)hashtable_next(iter)))
        {
            AVRO_TABLE *table = hashtable_fetch(router->open_tables, key);

            if (table)
            {
                if (flush == AVROROUTER_FLUSH)
                {
                    avro_file_writer_flush(table->avro_file);
                }
                else
                {
                    ss_dassert(flush == AVROROUTER_SYNC);
                    avro_file_writer_sync(table->avro_file);
                }
            }
        }
        hashtable_iterator_free(iter);
    }
}

/**
 * @brief Detection of table creation statements
 * @param router Avro router instance
 * @param ptr Pointer to statement
 * @param len Statement length
 * @return True if the statement creates a new table
 */
bool is_create_table_statement(AVRO_INSTANCE *router, char* ptr, size_t len)
{
    int rc = 0;
    pcre2_match_data *mdata = pcre2_match_data_create_from_pattern(router->create_table_re, NULL);

    if (mdata)
    {
        rc = pcre2_match(router->create_table_re, (PCRE2_SPTR) ptr, len, 0, 0, mdata, NULL);
        pcre2_match_data_free(mdata);
    }

    return rc > 0;
}

bool is_create_like_statement(const char* ptr, size_t len)
{
    char sql[len + 1];
    memcpy(sql, ptr, len);
    sql[len] = '\0';

    // This is not pretty but it should work
    return strcasestr(sql, " like ") || strcasestr(sql, "(like ");
}

/**
 * @brief Detection of table alteration statements
 * @param router Avro router instance
 * @param ptr Pointer to statement
 * @param len Statement length
 * @return True if the statement alters a table
 */
bool is_alter_table_statement(AVRO_INSTANCE *router, char* ptr, size_t len)
{
    int rc = 0;
    pcre2_match_data *mdata = pcre2_match_data_create_from_pattern(router->alter_table_re, NULL);

    if (mdata)
    {
        rc = pcre2_match(router->alter_table_re, (PCRE2_SPTR) ptr, len, 0, 0, mdata, NULL);
        pcre2_match_data_free(mdata);
    }

    return rc > 0;
}

/** Database name offset */
#define DBNM_OFF 8

/** Varblock offset */
#define VBLK_OFF 4 + 4 + 1 + 2

/** Post-header offset */
#define PHDR_OFF 4 + 4 + 1 + 2 + 2

/**
 * Save the CREATE TABLE statement to disk and replace older versions of the table
 * in the router's hashtable.
 * @param router Avro router instance
 * @param created Created table
 * @return False if an error occurred and true if successful
 */
bool save_and_replace_table_create(AVRO_INSTANCE *router, TABLE_CREATE *created)
{
    char table_ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2];
    snprintf(table_ident, sizeof(table_ident), "%s.%s", created->database, created->table);

    spinlock_acquire(&router->lock); // Is this necessary?
    TABLE_CREATE *old = hashtable_fetch(router->created_tables, table_ident);

    if (old)
    {
        HASHITERATOR *iter = hashtable_iterator(router->table_maps);

        char *key;
        while ((key = hashtable_next(iter)))
        {
            if (strcmp(key, table_ident) == 0)
            {
                TABLE_MAP* map = hashtable_fetch(router->table_maps, key);
                router->active_maps[map->id % MAX_MAPPED_TABLES] = NULL;
                hashtable_delete(router->table_maps, key);
            }
        }

        hashtable_iterator_free(iter);

        hashtable_delete(router->created_tables, table_ident);
    }

    hashtable_add(router->created_tables, table_ident, created);
    ss_dassert(created->columns > 0);
    spinlock_release(&router->lock);
    return true;
}

void unify_whitespace(char *sql, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (isspace(sql[i]) && sql[i] != ' ')
        {
            sql[i] = ' ';
        }
    }
}

/**
 * @brief Handling of query events
 *
 * @param router Avro router instance
 * @param hdr Replication header
 * @param pending_transaction Pointer where status of pending transaction is stored
 * @param ptr Pointer to the start of the event payload
 */
void handle_query_event(AVRO_INSTANCE *router, REP_HEADER *hdr, int *pending_transaction, uint8_t *ptr)
{
    int dblen = ptr[DBNM_OFF];
    int vblklen = ptr[VBLK_OFF];
    int len = hdr->event_size - BINLOG_EVENT_HDR_LEN - (PHDR_OFF + vblklen + 1 + dblen);
    char *sql = (char *) ptr + PHDR_OFF + vblklen + 1 + dblen;
    char db[dblen + 1];
    memcpy(db, (char*) ptr + PHDR_OFF + vblklen, dblen);
    db[dblen] = 0;

    size_t sqlsz = len, tmpsz = len;
    char *tmp = MXS_MALLOC(len);
    MXS_ABORT_IF_NULL(tmp);
    remove_mysql_comments((const char**)&sql, &sqlsz, &tmp, &tmpsz);
    sql = tmp;
    len = tmpsz;
    unify_whitespace(sql, len);

    if (is_create_table_statement(router, sql, len))
    {
        TABLE_CREATE *created = NULL;

        if (is_create_like_statement(sql, len))
        {
            created = table_create_copy(router, sql, len, db);
        }
        else
        {
            created = table_create_alloc(sql, len, db);
        }

        if (created && !save_and_replace_table_create(router, created))
        {
            MXS_ERROR("Failed to save statement to disk: %.*s", len, sql);
        }
    }
    else if (is_alter_table_statement(router, sql, len))
    {
        char ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2];
        read_alter_identifier(sql, sql + len, ident, sizeof(ident));

        bool combine = (strnlen(db, 1) && strchr(ident, '.') == NULL);

        size_t ident_len = strlen(ident) + 1; // + 1 for the NULL

        if (combine)
        {
            ident_len += (strlen(db) + 1); // + 1 for the "."
        }

        char full_ident[ident_len];
        full_ident[0] = 0; // Set full_ident to "".

        if (combine)
        {
            strcat(full_ident, db);
            strcat(full_ident, ".");
        }

        strcat(full_ident, ident);

        TABLE_CREATE *created = hashtable_fetch(router->created_tables, full_ident);

        if (created)
        {
            table_create_alter(created, sql, sql + len);
        }
        else
        {
            MXS_ERROR("Alter statement to a table with no create statement.");
        }
    }
    /* A transaction starts with this event */
    else if (strncmp(sql, "BEGIN", 5) == 0)
    {
        *pending_transaction = 1;
    }
    /* Commit received for non transactional tables, i.e. MyISAM */
    else if (strncmp(sql, "COMMIT", 6) == 0)
    {
        // TODO: Handle COMMIT
        *pending_transaction = 0;
    }

    MXS_FREE(tmp);
}
