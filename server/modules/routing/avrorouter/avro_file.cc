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
 * @file avro_file.c - File operations for the Avro router
 *
 * This file contains functions that handle the low level file operations for
 * the Avro router. The handling of Avro data files is done via the Avro C API
 * but the handling of MySQL format binary logs is done manually.
 *
 * Parts of this file have been copied from blr_file.c and modified for other
 * uses.
 */

#include "avrorouter.hh"
#include <maxscale/query_classifier.h>

#include <binlog_common.h>
#include <blr_constants.h>
#include <glob.h>
#include <ini.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <maxscale/alloc.h>
#include <maxscale/log.h>
#include <maxscale/pcre2.h>
#include <maxscale/utils.h>

static const char *statefile_section = "avro-conversion";


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
 * @brief Write a new ini file with current conversion status
 *
 * The file is stored in the cache directory as 'avro-conversion.ini'.
 * @param router Avro router instance
 * @return True if the file was written successfully to disk
 *
 */
bool avro_save_conversion_state(Avro *router)
{
    FILE *config_file;
    char filename[PATH_MAX + 1];

    snprintf(filename, sizeof(filename), "%s/" AVRO_PROGRESS_FILE ".tmp", router->avrodir.c_str());

    /* open file for writing */
    config_file = fopen(filename, "wb");

    if (config_file == NULL)
    {
        MXS_ERROR("Failed to open file '%s': %d, %s", filename,
                  errno, mxs_strerror(errno));
        return false;
    }

    gtid_pos_t gtid = router->handler.get_gtid();
    fprintf(config_file, "[%s]\n", statefile_section);
    fprintf(config_file, "position=%lu\n", router->current_pos);
    fprintf(config_file, "gtid=%lu-%lu-%lu:%lu\n", gtid.domain,
            gtid.server_id, gtid.seq, gtid.event_num);
    fprintf(config_file, "file=%s\n", router->binlog_name.c_str());
    fclose(config_file);

    /* rename tmp file to right filename */
    char newname[PATH_MAX + 1];
    snprintf(newname, sizeof(newname), "%s/" AVRO_PROGRESS_FILE, router->avrodir.c_str());
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
    Avro *router = (Avro*) data;

    if (strcmp(section, statefile_section) == 0)
    {
        if (strcmp(key, "gtid") == 0)
        {
            gtid_pos_t gtid;
            MXB_AT_DEBUG(bool rval = )gtid.parse(value);
            ss_dassert(rval);
            router->handler.set_gtid(gtid);
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

            router->binlog_name = value;
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
bool avro_load_conversion_state(Avro *router)
{
    char filename[PATH_MAX + 1];
    bool rval = false;

    snprintf(filename, sizeof(filename), "%s/" AVRO_PROGRESS_FILE, router->avrodir.c_str());

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
        {
            rval = true;
            gtid_pos_t gtid = router->handler.get_gtid();
            MXS_NOTICE("Loaded stored binary log conversion state: File: [%s] Position: [%ld] GTID: [%lu-%lu-%lu:%lu]",
                       router->binlog_name.c_str(), router->current_pos, gtid.domain,
                       gtid.server_id, gtid.seq, gtid.event_num);
        }
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
 * @brief Rotate to next file if it exists
 *
 * @param router Avro router instance
 * @param pos Current position, used for logging
 * @param stop_seen If a stop event was seen when processing current file
 * @return AVRO_OK if the next file exists, AVRO_LAST_FILE if this is the last
 * available file.
 */
static avro_binlog_end_t rotate_to_next_file_if_exists(Avro* router, uint64_t pos)
{
    avro_binlog_end_t rval = AVRO_LAST_FILE;

    if (binlog_next_file_exists(router->binlogdir.c_str(), router->binlog_name.c_str()))
    {
        char next_binlog[BINLOG_FNAMELEN + 1];
        if (snprintf(next_binlog, sizeof(next_binlog),
                     BINLOG_NAMEFMT, router->filestem.c_str(),
                     blr_file_get_next_binlogname(router->binlog_name.c_str())) >=  (int)sizeof(next_binlog))
        {
            MXS_ERROR("Next binlog name did not fit into the allocated buffer "
                      "but was truncated, aborting: %s", next_binlog);
            rval = AVRO_BINLOG_ERROR;
        }
        else
        {
            MXS_INFO("End of binlog file [%s] at %lu. Rotating to next binlog file [%s].",
                     router->binlog_name.c_str(), pos, next_binlog);
            rval = AVRO_OK;
            router->binlog_name = next_binlog;
            router->current_pos = 4;
        }
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
static void rotate_to_file(Avro* router, uint64_t pos, const char *next_binlog)
{
    MXS_NOTICE("End of binlog file [%s] at %lu. Rotating to file [%s].",
               router->binlog_name.c_str(), pos, next_binlog);
    router->binlog_name = next_binlog;
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
static GWBUF* read_event_data(Avro *router, REP_HEADER* hdr, uint64_t pos)
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

        if (n != static_cast<int>(hdr->event_size - BINLOG_EVENT_HDR_LEN))
        {
            if (n == -1)
            {
                MXS_ERROR("Error reading the event at %lu in %s. "
                          "%s, expected %d bytes.",
                          pos, router->binlog_name.c_str(),
                          mxs_strerror(errno),
                          hdr->event_size - BINLOG_EVENT_HDR_LEN);
            }
            else
            {
                MXS_ERROR("Short read when reading the event at %lu in %s. "
                          "Expected %d bytes got %d bytes.",
                          pos, router->binlog_name.c_str(),
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

bool notify_cb(DCB* dcb, void* data)
{
    SERVICE* service = static_cast<SERVICE*>(data);

    if (dcb->service == service && dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        poll_fake_write_event(dcb);
    }

    return true;
}

void notify_all_clients(SERVICE* service)
{
    dcb_foreach(notify_cb, service);
}

void do_checkpoint(Avro *router)
{
    router->handler.flush();
    avro_save_conversion_state(router);
    notify_all_clients(router->service);
    router->row_count = router->trx_count = 0;
}

bool read_header(Avro* router, unsigned long long pos, REP_HEADER* hdr, avro_binlog_end_t* rc)
{
    uint8_t hdbuf[BINLOG_EVENT_HDR_LEN];
    int n = pread(router->binlog_fd, hdbuf, BINLOG_EVENT_HDR_LEN, pos);

    /* Read the header information from the file */
    if (n != BINLOG_EVENT_HDR_LEN)
    {
        switch (n)
        {
        case 0:
            break;

        case -1:
            MXS_ERROR("Failed to read binlog file %s at position %llu (%s).",
                      router->binlog_name.c_str(), pos,
                      mxs_strerror(errno));
            break;

        default:
            MXS_ERROR("Short read when reading the header. "
                      "Expected 19 bytes but got %d bytes. "
                      "Binlog file is %s, position %llu",
                      n, router->binlog_name.c_str(), pos);
            break;
        }

        router->current_pos = pos;
        *rc = n == 0 ? AVRO_OK : AVRO_BINLOG_ERROR;
        return false;
    }

    bool rval = true;

    *hdr = construct_header(hdbuf);

    if (hdr->event_type > MAX_EVENT_TYPE_MARIADB10)
    {
        MXS_ERROR("Invalid MariaDB 10 event type 0x%x. Binlog file is %s, position %llu",
                  hdr->event_type, router->binlog_name.c_str(), pos);
        router->current_pos = pos;
        *rc = AVRO_BINLOG_ERROR;
        rval = false;
    }
    else if (hdr->event_size <= 0)
    {
        MXS_ERROR("Event size error: size %d at %llu.", hdr->event_size, pos);
        router->current_pos = pos;
        *rc = AVRO_BINLOG_ERROR;
        rval = false;
    }

    return rval;
}

static bool pos_is_ok(Avro* router, const REP_HEADER& hdr, uint64_t pos)
{
    bool rval = false;

    if (hdr.next_pos > 0 && hdr.next_pos < pos)
    {
        MXS_INFO("Binlog %s: next pos %u < pos %lu, truncating to %lu",
                 router->binlog_name.c_str(), hdr.next_pos, pos, pos);
    }
    else if (hdr.next_pos > 0 && hdr.next_pos != (pos + hdr.event_size))
    {
        MXS_INFO("Binlog %s: next pos %u != (pos %lu + event_size %u), truncating to %lu",
                 router->binlog_name.c_str(), hdr.next_pos, pos, hdr.event_size, pos);
    }
    else if (hdr.next_pos > 0)
    {
        rval = true;
    }
    else
    {
        MXS_ERROR("Current event type %d @ %lu has nex pos = %u : exiting",
                  hdr.event_type, pos, hdr.next_pos);
    }

    return rval;
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
avro_binlog_end_t avro_read_all_events(Avro *router)
{
    uint64_t pos = router->current_pos;
    std::string next_binlog;
    bool rotate_seen = false;

    ss_dassert(router->binlog_fd != -1);

    while (!service_should_stop)
    {
        avro_binlog_end_t rc;
        REP_HEADER hdr;

        if (!read_header(router, pos, &hdr, &rc))
        {
            if (rc == AVRO_OK)
            {
                do_checkpoint(router);

                if (rotate_seen)
                {
                    rotate_to_file(router, pos, next_binlog.c_str());
                }
                else
                {
                    rc = rotate_to_next_file_if_exists(router, pos);
                }
            }
            return rc;
        }

        GWBUF *result = read_event_data(router, &hdr, pos);

        if (result == NULL)
        {
            router->current_pos = pos;
            return AVRO_BINLOG_ERROR;
        }

        /* get event content */
        uint8_t* ptr = GWBUF_DATA(result);

        // These events are only related to binary log files
        if (hdr.event_type == ROTATE_EVENT)
        {
            int len = hdr.event_size - BINLOG_EVENT_HDR_LEN - 8 - (router->handler.have_checksums() ? 4 : 0);
            next_binlog.assign((char*)ptr + 8, len);
            rotate_seen = true;
        }
        else if (hdr.event_type == MARIADB_ANNOTATE_ROWS_EVENT)
        {
            // This appears to need special handling
            int annotate_len = hdr.event_size - BINLOG_EVENT_HDR_LEN - (router->handler.have_checksums() ? 4 : 0);
            MXS_INFO("Annotate_rows_event: %.*s", annotate_len, ptr);
            pos += hdr.event_size;
            router->current_pos = pos;
            gwbuf_free(result);
            continue;
        }
        else
        {
            if ((hdr.event_type >= WRITE_ROWS_EVENTv0 && hdr.event_type <= DELETE_ROWS_EVENTv1) ||
                (hdr.event_type >= WRITE_ROWS_EVENTv2 && hdr.event_type <= DELETE_ROWS_EVENTv2))
            {
                router->row_count++;
            }
            else if (hdr.event_type == XID_EVENT)
            {
                router->trx_count++;
            }

            router->handler.handle_event(hdr, ptr);
        }

        gwbuf_free(result);

        if (router->row_count >= router->row_target ||
            router->trx_count >= router->trx_target)
        {
            do_checkpoint(router);
        }

        if (pos_is_ok(router, hdr, pos))
        {
            pos = hdr.next_pos;
            router->current_pos = pos;
        }
        else
        {
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
void avro_load_metadata_from_schemas(Avro *router)
{
    char path[PATH_MAX + 1];
    snprintf(path, sizeof(path), "%s/*.avsc", router->avrodir.c_str());
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
                STableCreateEvent created(table_create_from_schema(files.gl_pathv[i],
                                                                   db, table, version));
                router->handler.add_create(created);
            }
            else
            {
                MXS_ERROR("Malformed schema file name: %s", files.gl_pathv[i]);
            }
        }
    }

    globfree(&files);
}
