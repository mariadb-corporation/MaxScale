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

#include "avrorouter.hh"
#include <maxscale/query_classifier.h>

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

void handle_query_event(Avro *router, REP_HEADER *hdr, uint8_t *ptr);

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

    fprintf(config_file, "[%s]\n", statefile_section);
    fprintf(config_file, "position=%lu\n", router->current_pos);
    fprintf(config_file, "gtid=%lu-%lu-%lu:%lu\n", router->gtid.domain,
            router->gtid.server_id, router->gtid.seq, router->gtid.event_num);
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
        rval = true;
        MXS_NOTICE("Loaded stored binary log conversion state: File: [%s] Position: [%ld] GTID: [%lu-%lu-%lu:%lu]",
                   router->binlog_name.c_str(), router->current_pos, router->gtid.domain,
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
 * @brief Rotate to next file if it exists
 *
 * @param router Avro router instance
 * @param pos Current position, used for logging
 * @param stop_seen If a stop event was seen when processing current file
 * @return AVRO_OK if the next file exists, AVRO_LAST_FILE if this is the last
 * available file.
 */
static avro_binlog_end_t rotate_to_next_file_if_exists(Avro* router, uint64_t pos, bool stop_seen)
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
            if (stop_seen)
            {
                MXS_NOTICE("End of binlog file [%s] at %lu with a "
                           "close event. Rotating to next binlog file [%s].",
                           router->binlog_name.c_str(), pos, next_binlog);
            }
            else
            {
                MXS_NOTICE("End of binlog file [%s] at %lu with no "
                           "close or rotate event. Rotating to next binlog file [%s].",
                           router->binlog_name.c_str(), pos, next_binlog);
            }

            rval = AVRO_OK;
            router->binlog_name = next_binlog;
            router->current_pos = 4;
        }
    }
    else if (stop_seen)
    {
        MXS_NOTICE("End of binlog file [%s] at %lu with a close event. "
                   "Next binlog file does not exist, pausing file conversion.",
                   router->binlog_name.c_str(), pos);
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

void notify_all_clients(Avro *router)
{
    dcb_foreach(notify_cb, router->service);
}

void do_checkpoint(Avro *router)
{
    router->event_hander->flush_tables();
    avro_save_conversion_state(router);
    notify_all_clients(router);
    router->row_count = router->trx_count = 0;
}

REP_HEADER construct_header(uint8_t* ptr)
{
    REP_HEADER hdr;

    hdr.timestamp = EXTRACT32(ptr);
    hdr.event_type = ptr[4];
    hdr.serverid = EXTRACT32(&ptr[5]);
    hdr.event_size = extract_field(&ptr[9], 32);
    hdr.next_pos = EXTRACT32(&ptr[13]);
    hdr.flags = EXTRACT16(&ptr[17]);

    return hdr;
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

static bool pos_is_ok(Avro* router, const REP_HEADER& hdr, uint64_t pos, uint64_t original_size)
{
    bool rval = false;

    if (hdr.next_pos > 0 && hdr.next_pos < pos)
    {
        MXS_INFO("Binlog %s: next pos %u < pos %lu, truncating to %lu",
                 router->binlog_name.c_str(), hdr.next_pos, pos, pos);
    }
    else if (hdr.next_pos > 0 && hdr.next_pos != (pos + original_size))
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

void handle_one_event(Avro* router, uint8_t* ptr, REP_HEADER& hdr, uint64_t& pos)
{
    if (router->binlog_checksum)
    {
        hdr.event_size -= 4;
    }

    // The following events are related to the actual data
    if (hdr.event_type == FORMAT_DESCRIPTION_EVENT)
    {
        const int BLRM_FDE_EVENT_TYPES_OFFSET = 2 + 50 + 4 + 1;
        const int FDE_EXTRA_BYTES = 5;
        int event_header_length = ptr[BLRM_FDE_EVENT_TYPES_OFFSET - 1];
        int n_events = hdr.event_size - event_header_length - BLRM_FDE_EVENT_TYPES_OFFSET - FDE_EXTRA_BYTES;
        uint8_t* checksum = ptr + hdr.event_size - event_header_length - FDE_EXTRA_BYTES;

        // Precaution to prevent writing too much in case new events are added
        int real_len = MXS_MIN(n_events, (int)sizeof(router->event_type_hdr_lens));
        memcpy(router->event_type_hdr_lens, ptr + BLRM_FDE_EVENT_TYPES_OFFSET, real_len);

        router->event_types = n_events;
        router->binlog_checksum = checksum[0];
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
    else if (hdr.event_type == MARIADB10_GTID_EVENT)
    {
        router->gtid.domain = extract_field(ptr + 8, 32);
        router->gtid.server_id = hdr.serverid;
        router->gtid.seq = extract_field(ptr, 64);
        router->gtid.event_num = 0;
        router->gtid.timestamp = hdr.timestamp;
    }
    else if (hdr.event_type == QUERY_EVENT)
    {
        handle_query_event(router, &hdr, ptr);
    }
    else if (hdr.event_type == XID_EVENT)
    {
        router->trx_count++;

        if (router->row_count >= router->row_target ||
            router->trx_count >= router->trx_target)
        {
            do_checkpoint(router);
        }
    }
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
    char next_binlog[BINLOG_FNAMELEN + 1];
    bool found_chksum = false;
    bool rotate_seen = false;
    bool stop_seen = false;

    ss_dassert(router->binlog_fd != -1);

    while (!router->service->svc_do_shutdown)
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
                    rotate_to_file(router, pos, next_binlog);
                }
                else
                {
                    rc = rotate_to_next_file_if_exists(router, pos, stop_seen);
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

        uint64_t original_size = hdr.event_size;

        /* get event content */
        uint8_t* ptr = GWBUF_DATA(result);

        // These events are only related to binary log files
        if (hdr.event_type == STOP_EVENT)
        {
            char next_file[BLRM_BINLOG_NAME_STR_LEN + 1];
            stop_seen = true;
            snprintf(next_file, sizeof(next_file), BINLOG_NAMEFMT, router->filestem.c_str(),
                     blr_file_get_next_binlogname(router->binlog_name.c_str()));
        }
        else if (hdr.event_type == ROTATE_EVENT)
        {
            int len = hdr.event_size - BINLOG_EVENT_HDR_LEN - 8;

            if (found_chksum || router->binlog_checksum)
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
        else if (hdr.event_type == MARIADB_ANNOTATE_ROWS_EVENT)
        {
            // This appears to need special handling
            int annotate_len = hdr.event_size - BINLOG_EVENT_HDR_LEN - (found_chksum || router->binlog_checksum ? 4 : 0);
            MXS_INFO("Annotate_rows_event: %.*s", annotate_len, ptr);
            pos += hdr.event_size;
            router->current_pos = pos;
            gwbuf_free(result);
            continue;
        }
        else
        {
            handle_one_event(router, ptr, hdr, pos);
        }

        if (pos_is_ok(router, hdr, pos, original_size))
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
                auto it = router->created_tables.find(table_ident);

                if (it == router->created_tables.end() || version > it->second->version)
                {
                    STableCreateEvent created(table_create_from_schema(files.gl_pathv[i],
                                                                       db, table, version));
                    router->created_tables[table_ident] = created;
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
 * @brief Detection of table creation statements
 * @param router Avro router instance
 * @param ptr Pointer to statement
 * @param len Statement length
 * @return True if the statement creates a new table
 */
bool is_create_table_statement(Avro *router, char* ptr, size_t len)
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

bool is_create_as_statement(const char* ptr, size_t len)
{
    int err = 0;
    char sql[len + 1];
    memcpy(sql, ptr, len);
    sql[len] = '\0';
    const char* pattern =
        // Case-insensitive mode
        "(?i)"
        // Main CREATE TABLE part (the \s is for any whitespace)
        "create\\stable\\s"
        // Optional IF NOT EXISTS
        "(if\\snot\\sexists\\s)?"
        // The table name with optional database name, both enclosed in optional backticks
        "(`?\\S+`?.)`?\\S+`?\\s"
        // And finally the AS keyword
        "as";

    return mxs_pcre2_simple_match(pattern, sql, 0, &err) == MXS_PCRE2_MATCH;
}

/**
 * @brief Detection of table alteration statements
 * @param router Avro router instance
 * @param ptr Pointer to statement
 * @param len Statement length
 * @return True if the statement alters a table
 */
bool is_alter_table_statement(Avro *router, char* ptr, size_t len)
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
bool save_and_replace_table_create(Avro *router, TableCreateEvent *created)
{
    std::string table_ident = created->database + "." + created->table;
    auto it = router->created_tables.find(table_ident);

    if (it != router->created_tables.end())
    {
        auto tm_it = router->table_maps.find(table_ident);

        if (tm_it != router->table_maps.end())
        {
            router->active_maps.erase(tm_it->second->id);
            router->table_maps.erase(tm_it);
        }
    }

    router->created_tables[table_ident] = STableCreateEvent(created);
    ss_dassert(created->columns.size() > 0);
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
 * A very simple function for stripping auto-generated executable comments
 *
 * Note that the string will not strip the trailing part of the comment, making
 * the SQL invalid.
 *
 * @param sql String to modify
 * @param len Pointer to current length of string, updated to new length if
 *            @c sql is modified
 */
static void strip_executable_comments(char *sql, int* len)
{
    if (strncmp(sql, "/*!", 3) == 0 || strncmp(sql, "/*M!", 4) == 0)
    {
        // Executable comment, remove it
        char* p = sql + 3;
        if (*p == '!')
        {
            p++;
        }

        // Skip the versioning part
        while (*p && isdigit(*p))
        {
            p++;
        }

        int n_extra = p - sql;
        int new_len = *len - n_extra;
        memmove(sql, sql + n_extra, new_len);
        *len = new_len;
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
void handle_query_event(Avro *router, REP_HEADER *hdr, uint8_t *ptr)
{
    int dblen = ptr[DBNM_OFF];
    int vblklen = gw_mysql_get_byte2(ptr + VBLK_OFF);
    int len = hdr->event_size - BINLOG_EVENT_HDR_LEN - (PHDR_OFF + vblklen + 1 + dblen);
    char *sql = (char *) ptr + PHDR_OFF + vblklen + 1 + dblen;
    char db[dblen + 1];
    memcpy(db, (char*) ptr + PHDR_OFF + vblklen, dblen);
    db[dblen] = 0;

    size_t sqlsz = len, tmpsz = len;
    char *tmp = static_cast<char*>(MXS_MALLOC(len + 1));
    MXS_ABORT_IF_NULL(tmp);
    remove_mysql_comments((const char**)&sql, &sqlsz, &tmp, &tmpsz);
    sql = tmp;
    len = tmpsz;
    unify_whitespace(sql, len);
    strip_executable_comments(sql, &len);
    sql[len] = '\0';

    if (*sql == '\0')
    {
        MXS_FREE(tmp);
        return;
    }

    static bool warn_not_row_format = true;

    if (warn_not_row_format)
    {
        GWBUF* buffer = gwbuf_alloc(len + 5);
        gw_mysql_set_byte3(GWBUF_DATA(buffer), len + 1);
        GWBUF_DATA(buffer)[4] = 0x03;
        memcpy(GWBUF_DATA(buffer) + 5, sql, len);
        qc_query_op_t op = qc_get_operation(buffer);
        gwbuf_free(buffer);

        if (op == QUERY_OP_UPDATE || op == QUERY_OP_INSERT || op == QUERY_OP_DELETE)
        {
            MXS_WARNING("Possible STATEMENT or MIXED format binary log. Check that "
                        "'binlog_format' is set to ROW on the master.");
            warn_not_row_format = false;
        }
    }

    char ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2];
    read_table_identifier(db, sql, sql + len, ident, sizeof(ident));

    if (is_create_table_statement(router, sql, len))
    {
        TableCreateEvent *created = NULL;

        if (is_create_like_statement(sql, len))
        {
            created = table_create_copy(router, sql, len, db);
        }
        else if (is_create_as_statement(sql, len))
        {
            static bool warn_create_as = true;
            if (warn_create_as)
            {
                MXS_WARNING("`CREATE TABLE AS` is not yet supported, ignoring events to this table: %.*s", len, sql);
                warn_create_as = false;
            }
        }
        else
        {
            created = table_create_alloc(ident, sql, len);
        }

        if (created && !save_and_replace_table_create(router, created))
        {
            MXS_ERROR("Failed to save statement to disk: %.*s", len, sql);
        }
    }
    else if (is_alter_table_statement(router, sql, len))
    {
        auto it = router->created_tables.find(ident);

        if (it != router->created_tables.end())
        {
            table_create_alter(it->second.get(), sql, sql + len);
        }
        else
        {
            MXS_ERROR("Alter statement to table '%s' has no preceding create statement.", ident);
        }
    }
    /* Commit received for non transactional tables, i.e. MyISAM */
    else if (strncmp(sql, "COMMIT", 6) == 0)
    {
        router->trx_count++;
    }

    MXS_FREE(tmp);
}
