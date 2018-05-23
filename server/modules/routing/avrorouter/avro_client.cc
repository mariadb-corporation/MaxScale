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
 * @file avro_client.c - contains code for the AVRO router to client communication
 */

#include "avrorouter.hh"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <maxscale/service.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxscale/atomic.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/log_manager.h>
#include <maxscale/version.h>
#include <maxavro.h>
#include <maxscale/alloc.h>

extern char *blr_extract_column(GWBUF *buf, int col);
extern uint32_t extract_field(uint8_t *src, int bits);

/* AVRO */
static int avro_client_do_registration(Avro *, AvroSession *, GWBUF *);
int avro_client_callback(DCB *dcb, DCB_REASON reason, void *data);
static void avro_client_process_command(Avro *router, AvroSession *client, GWBUF *queue);
static bool avro_client_stream_data(AvroSession *client);
void avro_notify_client(AvroSession *client);
void poll_fake_write_event(DCB *dcb);
GWBUF* read_avro_json_schema(const char *avrofile, const char* dir);
GWBUF* read_avro_binary_schema(const char *avrofile, const char* dir);
const char* get_avrofile_name(const char *file_ptr, int data_len, char *dest);

/**
 * Process a request packet from the slave server.
 *
 * @param router    The router instance this defines the master for this replication chain
 * @param client    The client specific data
 * @param queue     The incoming request packet
 * @return 1 on success, 0 on error or failure
 */
int
avro_client_handle_request(Avro *router, AvroSession *client, GWBUF *queue)
{
    int rval = 1;

    switch (client->state)
    {
    case AVRO_CLIENT_ERRORED:
        /* force disconnection */
        return 0;
        break;
    case AVRO_CLIENT_UNREGISTERED:
        if (avro_client_do_registration(router, client, queue) == 0)
        {
            client->state = AVRO_CLIENT_ERRORED;
            dcb_printf(client->dcb, "ERR, code 12, msg: Registration failed\n");
            /* force disconnection */
            dcb_close(client->dcb);
            rval = 0;
        }
        else
        {
            /* Send OK ack to client */
            dcb_printf(client->dcb, "OK\n");

            client->state = AVRO_CLIENT_REGISTERED;
            MXS_INFO("%s: Client [%s] has completed REGISTRATION action",
                     client->dcb->service->name,
                     client->dcb->remote != NULL ? client->dcb->remote : "");
        }
        break;
    case AVRO_CLIENT_REGISTERED:
    case AVRO_CLIENT_REQUEST_DATA:
        if (client->state == AVRO_CLIENT_REGISTERED)
        {
            client->state = AVRO_CLIENT_REQUEST_DATA;
        }

        /* Process command from client */
        avro_client_process_command(router, client, queue);

        break;
    default:
        client->state = AVRO_CLIENT_ERRORED;
        rval = 0;
        break;
    }

    gwbuf_free(queue);

    return rval;
}

/**
 * Handle the REGISTRATION command
 *
 * @param dcb    DCB with allocateid protocol
 * @param data   GWBUF with registration message
 * @return       1 for successful registration 0 otherwise
 *
 */
static int
avro_client_do_registration(Avro *router, AvroSession *client, GWBUF *data)
{
    const char reg_uuid[] = "REGISTER UUID=";
    const int reg_uuid_len = strlen(reg_uuid);
    int data_len = GWBUF_LENGTH(data) - reg_uuid_len;
    char *request = (char*)GWBUF_DATA(data);
    int ret = 0;

    if (strstr(request, reg_uuid) != NULL)
    {
        char *sep_ptr;
        int uuid_len = (data_len > CDC_UUID_LEN) ? CDC_UUID_LEN : data_len;
        /* 36 +1 */
        char uuid[uuid_len + 1];
        memcpy(uuid, request + reg_uuid_len, uuid_len);
        uuid[uuid_len] = '\0';

        if ((sep_ptr = strchr(uuid, ',')) != NULL)
        {
            *sep_ptr = '\0';
        }
        if ((sep_ptr = strchr(uuid + strlen(uuid), ' ')) != NULL)
        {
            *sep_ptr = '\0';
        }
        if ((sep_ptr = strchr(uuid, ' ')) != NULL)
        {
            *sep_ptr = '\0';
        }

        if (strlen(uuid) < static_cast<size_t>(uuid_len))
        {
            data_len -= (uuid_len - strlen(uuid));
        }

        uuid_len = strlen(uuid);

        client->uuid = MXS_STRDUP_A(uuid);

        if (data_len > 0)
        {
            /* Check for CDC request type */
            char *tmp_ptr = strstr(request + sizeof(reg_uuid) + uuid_len, "TYPE=");
            if (tmp_ptr)
            {
                if (memcmp(tmp_ptr + 5, "AVRO", 4) == 0)
                {
                    ret = 1;
                    client->state = AVRO_CLIENT_REGISTERED;
                    client->format = AVRO_FORMAT_AVRO;
                }
                else if (memcmp(tmp_ptr + 5, "JSON", 4) == 0)
                {
                    ret = 1;
                    client->state = AVRO_CLIENT_REGISTERED;
                    client->format = AVRO_FORMAT_JSON;
                }
                else
                {
                    fprintf(stderr, "Registration TYPE not supported, only AVRO\n");
                }
            }
            else
            {
                fprintf(stderr, "TYPE not found in Registration\n");
            }
        }
        else
        {
            fprintf(stderr, "Registration data_len = 0\n");
        }
    }
    return ret;
}

/**
 * Extract the GTID the client requested
 * @param gtid
 * @param start
 * @param end
 */
void extract_gtid_request(gtid_pos_t *gtid, const char *start, int len)
{
    const char *ptr = start;
    int read = 0;

    while (ptr < start + len)
    {
        if (!isdigit(*ptr))
        {
            ptr++;
        }
        else
        {
            char *end;
            switch (read)
            {
            case 0:
                gtid->domain = strtol(ptr, &end, 10);
                break;
            case 1:
                gtid->server_id = strtol(ptr, &end, 10);
                break;
            case 2:
                gtid->seq = strtol(ptr, &end, 10);
                break;
            }
            read++;
            ptr = end;
        }
    }
}

/**
 * Callback for GTID retrieval
 * @param data User data
 * @param ncolumns Number of columns
 * @param values Row data
 * @param names Field names
 * @return 0 on success
 */
int gtid_query_cb(void* data, int ncolumns, char** values, char** names)
{
    json_t *arr = (json_t*)data;

    if (values[0])
    {
        json_array_append_new(arr, json_string(values[0]));
    }

    return 0;
}

/**
 * Callback for GTID retrieval
 * @param data User data
 * @param ncolumns Number of columns
 * @param values Row data
 * @param names Field names
 * @return 0 on success
 */
int gtid_query_cb_plain(void* data, int ncolumns, char** values, char** names)
{
    DCB *dcb = (DCB *)data;
    if (values[0])
    {
        dcb_printf(dcb, "%s ", values[0]);
    }

    return 0;
}

/**
 * Add the tables involved in the latest transaction to a JSON object
 *
 * @param handle SQLite3 handle
 * @param obj JSON object to add values to
 * @param gtid GTID of the last transaction
 */
void add_used_tables(sqlite3 *handle, json_t* obj, gtid_pos_t* gtid)
{
    char sql[AVRO_SQL_BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "SELECT table_name FROM " USED_TABLES_TABLE_NAME
             " WHERE domain = %lu AND server_id = %lu AND sequence = %lu",
             gtid->domain, gtid->server_id, gtid->seq);

    char* errmsg;
    json_t *arr = json_array();

    if (sqlite3_exec(handle, sql, gtid_query_cb, arr,
                     &errmsg) != SQLITE_OK)
    {
        json_decref(arr);
        MXS_ERROR("Failed to execute query: %s", errmsg);
    }
    else
    {
        json_object_set_new(obj, "tables", arr);
    }
    sqlite3_free(errmsg);
}

/**
 * Get the tables involved in the latest transaction.
 *
 * Sqlite3 callback routine calls dcb_printf()
 *
 * @param router The AVRO router instance
 * @param dcb    The dcb to write data
 */
void avro_get_used_tables(Avro *router, DCB* dcb)
{
    sqlite3 *handle = router->sqlite_handle;
    char sql[AVRO_SQL_BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "SELECT table_name FROM " USED_TABLES_TABLE_NAME
             " WHERE domain = %lu AND server_id = %lu AND sequence = %lu",
             router->gtid.domain, router->gtid.server_id, router->gtid.seq);

    char* errmsg;

    /* call dcb_printf via callback */
    if (sqlite3_exec(handle, sql, gtid_query_cb_plain, dcb,
                     &errmsg) != SQLITE_OK)
    {
        MXS_ERROR("Failed to execute query: %s", errmsg);
    }
    sqlite3_free(errmsg);
}

/**
 * Callback for timestamp retrieval
 *
 * @param data User data
 * @param ncolumns Number of columns
 * @param values Row data
 * @param names Field names
 * @return 0 on success
 */
int timestamp_query_cb(void* data, int ncolumns, char** values, char** names)
{
    long *val = (long*)data;

    if (values[0])
    {
        *val = strtol(values[0], NULL, 10);
    }

    return 0;
}

/**
 * Add the GTID timestamp to a JSON object
 *
 * @param handle
 * @param obj
 * @param gtid
 */
void add_timestamp(sqlite3 *handle, json_t* obj, gtid_pos_t* gtid)
{
    char sql[AVRO_SQL_BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "SELECT DISTINCT binlog_timestamp FROM " USED_TABLES_TABLE_NAME
             " WHERE domain = %lu AND server_id = %lu AND sequence = %lu",
             gtid->domain, gtid->server_id, gtid->seq);

    char* errmsg;
    long ts = 0;

    if (sqlite3_exec(handle, sql, timestamp_query_cb, &ts,
                     &errmsg) == SQLITE_OK)
    {
        json_object_set_new(obj, "timestamp", json_integer(ts));
    }
    else
    {
        MXS_ERROR("Failed to execute query: %s", errmsg);
    }
    sqlite3_free(errmsg);

}

/**
 * Send information about the current GTID being processed
 * @param router Router instance
 * @param dcb Client DCB
 */
void send_gtid_info(Avro *router, gtid_pos_t *gtid_pos, DCB *dcb)
{
    json_t *obj = json_object();

    if (obj)
    {
        char gtid[256];
        snprintf(gtid, sizeof(gtid), "%lu-%lu-%lu", gtid_pos->domain,
                 gtid_pos->server_id, gtid_pos->seq);
        json_object_set_new(obj, "GTID", json_string(gtid));

        // TODO: Store number of events in the database
        json_object_set_new(obj, "events", json_integer(gtid_pos->event_num));

        add_timestamp(router->sqlite_handle, obj, gtid_pos);
        add_used_tables(router->sqlite_handle, obj, gtid_pos);

        char *js = json_dumps(obj, 0);
        size_t size = strlen(js);
        GWBUF *buffer = gwbuf_alloc_and_load(size, js);
        MXS_FREE(js);
        dcb->func.write(dcb, buffer);
    }
}

/**
 * @brief Check if a file exists in a directory
 *
 * @param dir Directory where the file is searched
 * @param file File to search
 * @return True if file exists
 */
bool file_in_dir(const char *dir, const char *file)
{
    char path[PATH_MAX + 1];

    snprintf(path, sizeof(path), "%s/%s", dir, file);

    return access(path, F_OK) == 0;
}

/**
 * Process command from client
 *
 * @param router     The router instance
 * @param client     The specific client data
 * @param data       GWBUF with command
 *
 */
static void
avro_client_process_command(Avro *router, AvroSession *client, GWBUF *queue)
{
    const char req_data[] = "REQUEST-DATA";
    const char req_last_gtid[] = "QUERY-LAST-TRANSACTION";
    const char req_gtid[] = "QUERY-TRANSACTION";
    const size_t req_data_len = sizeof(req_data) - 1;
    size_t buflen = gwbuf_length(queue);
    uint8_t data[buflen + 1];
    gwbuf_copy_data(queue, 0, buflen, data);
    data[buflen] = '\0';
    char *command_ptr = strstr((char *)data, req_data);

    if (command_ptr != NULL)
    {
        char *file_ptr = command_ptr + req_data_len;
        int data_len = GWBUF_LENGTH(queue) - req_data_len;

        if (data_len > 1)
        {
            const char *gtid_ptr = get_avrofile_name(file_ptr, data_len, client->avro_binfile);

            if (gtid_ptr)
            {
                client->requested_gtid = true;
                extract_gtid_request(&client->gtid, gtid_ptr, data_len - (gtid_ptr - file_ptr));
                memcpy(&client->gtid_start, &client->gtid, sizeof(client->gtid_start));
            }

            if (file_in_dir(router->avrodir.c_str(), client->avro_binfile))
            {
                /* set callback routine for data sending */
                dcb_add_callback(client->dcb, DCB_REASON_DRAINED, avro_client_callback, client);

                /* Add fake event that will call the avro_client_callback() routine */
                poll_fake_write_event(client->dcb);
            }
            else
            {
                dcb_printf(client->dcb, "ERR NO-FILE File '%s' not found.", client->avro_binfile);
            }
        }
        else
        {
            dcb_printf(client->dcb, "ERR REQUEST-DATA with no data");
        }
    }
    /* Return last GTID info */
    else if (strstr((char *)data, req_last_gtid))
    {
        send_gtid_info(router, &router->gtid, client->dcb);
    }
    /** Return requested GTID */
    else if (strstr((char *)data, req_gtid))
    {
        gtid_pos_t gtid;
        extract_gtid_request(&gtid, (char*)data + sizeof(req_gtid),
                             GWBUF_LENGTH(queue) - sizeof(req_gtid));
        send_gtid_info(router, &gtid, client->dcb);
    }
    else
    {
        GWBUF *reply = gwbuf_alloc(5);
        memcpy(GWBUF_DATA(reply), "ECHO:", 5);
        reply = gwbuf_append(reply, gwbuf_clone(queue));
        client->dcb->func.write(client->dcb, reply);
    }
}

/**
 * @brief Form the full Avro file name
 *
 * @param file_ptr Requested file
 * @param data_len Length of string pointed by @p file_ptr
 * @param dest Destination where the file name is stored. Must be at least
 * @p data_len + 1 bytes.
 */
const char* get_avrofile_name(const char *file_ptr, int data_len, char *dest)
{
    while (isspace(*file_ptr))
    {
        file_ptr++;
        data_len--;
    }

    char avro_file[data_len + 1];
    memcpy(avro_file, file_ptr, data_len);
    avro_file[data_len] = '\0';

    char *cmd_sep = strchr(avro_file, ' ');
    const char *rval = NULL;

    if (cmd_sep)
    {
        *cmd_sep++ = '\0';
        rval = file_ptr + (cmd_sep - avro_file);
        ss_dassert(rval < file_ptr + data_len);
    }

    /** Exact file version specified */
    if ((cmd_sep = strchr(avro_file, '.')) && (cmd_sep = strchr(cmd_sep + 1, '.')) &&
        strlen(cmd_sep + 1) > 0)
    {
        snprintf(dest, AVRO_MAX_FILENAME_LEN, "%s.avro", avro_file);
    }
    /** No version specified, send all files */
    else
    {
        snprintf(dest, AVRO_MAX_FILENAME_LEN, "%s.000001.avro", avro_file);
    }

    return rval;
}

static int send_row(DCB *dcb, json_t* row)
{
    char *json = json_dumps(row, JSON_PRESERVE_ORDER);
    size_t len = strlen(json);
    GWBUF *buf = gwbuf_alloc(len + 1);
    int rc = 0;

    if (json && buf)
    {
        uint8_t *data = GWBUF_DATA(buf);
        memcpy(data, json, len);
        data[len] = '\n';
        rc = dcb->func.write(dcb, buf);
    }
    else
    {
        MXS_ERROR("Failed to dump JSON value.");
        rc = 0;
    }
    MXS_FREE(json);
    return rc;
}

static void set_current_gtid(AvroSession *client, json_t *row)
{
    json_t *obj = json_object_get(row, avro_sequence);
    ss_dassert(json_is_integer(obj));
    client->gtid.seq = json_integer_value(obj);

    obj = json_object_get(row, avro_server_id);
    ss_dassert(json_is_integer(obj));
    client->gtid.server_id = json_integer_value(obj);

    obj = json_object_get(row, avro_domain);
    ss_dassert(json_is_integer(obj));
    client->gtid.domain = json_integer_value(obj);
}

/**
 * @brief Stream Avro data in JSON format
 *
 * @param file File to stream from
 * @param dcb DCB to stream to
 * @return True if more data is readable, false if all data was sent
 */
static bool stream_json(AvroSession *client)
{
    int bytes = 0;
    MAXAVRO_FILE *file = client->file_handle;
    DCB *dcb = client->dcb;

    do
    {
        json_t *row;
        int rc = 1;
        while (rc > 0 && (row = maxavro_record_read_json(file)))
        {
            rc = send_row(dcb, row);
            set_current_gtid(client, row);
            json_decref(row);
        }
        bytes += file->buffer_size;
    }
    while (maxavro_next_block(file) && bytes < AVRO_DATA_BURST_SIZE);

    return bytes >= AVRO_DATA_BURST_SIZE;
}

/**
 * @brief Stream Avro data in native Avro format
 *
 * @param file File to stream from
 * @param dcb DCB to stream to
 * @return True if streaming was successful, false if an error occurred
 */
static bool stream_binary(AvroSession *client)
{
    GWBUF *buffer;
    uint64_t bytes = 0;
    int rc = 1;
    MAXAVRO_FILE *file = client->file_handle;
    DCB *dcb = client->dcb;

    while (rc > 0 && bytes < AVRO_DATA_BURST_SIZE)
    {
        bytes += file->buffer_size;
        if ((buffer = maxavro_record_read_binary(file)))
        {
            rc = dcb->func.write(dcb, buffer);
        }
        else
        {
            rc = 0;
        }
    }

    return bytes >= AVRO_DATA_BURST_SIZE;
}

static int sqlite_cb(void* data, int rows, char** values, char** names)
{
    for (int i = 0; i < rows; i++)
    {
        if (values[i])
        {
            *((long*)data) = strtol(values[i], NULL, 10);
            return 0;
        }
    }
    return 0;
}

static const char select_template[] = "SELECT max(position) FROM gtid WHERE domain=%lu "
                                      "AND server_id=%lu AND sequence <= %lu AND avrofile=\"%s\";";

static bool seek_to_index_pos(AvroSession *client, MAXAVRO_FILE* file)
{
    char *name = strrchr(client->file_handle->filename, '/');
    ss_dassert(name);
    name++;

    char sql[sizeof(select_template) + NAME_MAX + 80];
    snprintf(sql, sizeof(sql), select_template, client->gtid.domain,
             client->gtid.server_id, client->gtid.seq, name);

    long offset = -1;
    char *errmsg = NULL;
    bool rval = false;

    if (sqlite3_exec(client->sqlite_handle, sql, sqlite_cb, &offset, &errmsg) == SQLITE_OK)
    {
        rval = true;
        if (offset > 0 && !maxavro_record_set_pos(file, offset))
        {
            rval = false;
        }
    }
    else
    {
        MXS_ERROR("Failed to query index position for GTID %lu-%lu-%lu: %s",
                  client->gtid.domain, client->gtid.server_id, client->gtid.seq, errmsg);
    }
    sqlite3_free(errmsg);
    return rval;
}

/**
 *
 * @param client
 * @param file
 */
static bool seek_to_gtid(AvroSession *client, MAXAVRO_FILE* file)
{
    bool seeking = true;

    do
    {
        json_t *row;
        while ((row = maxavro_record_read_json(file)))
        {
            json_t *obj = json_object_get(row, avro_sequence);
            ss_dassert(json_is_integer(obj));
            uint64_t value = json_integer_value(obj);

            /** If a larger GTID is found, use that */
            if (value >= client->gtid.seq)
            {
                obj = json_object_get(row, avro_server_id);
                ss_dassert(json_is_integer(obj));
                value = json_integer_value(obj);

                if (value == client->gtid.server_id)
                {
                    obj = json_object_get(row, avro_domain);
                    ss_dassert(json_is_integer(obj));
                    value = json_integer_value(obj);

                    if (value == client->gtid.domain)
                    {
                        MXS_INFO("Found GTID %lu-%lu-%lu for %s@%s",
                                 client->gtid.domain, client->gtid.server_id,
                                 client->gtid.seq, client->dcb->user, client->dcb->remote);
                        seeking = false;
                    }
                }
            }

            /** We'll send the first found row immediately since we have already
             * read the row into memory */
            if (!seeking)
            {
                send_row(client->dcb, row);
            }

            json_decref(row);
        }
    }
    while (seeking && maxavro_next_block(file));

    return !seeking;
}

/**
 * Print JSON output from selected AVRO file
 *
 * @param router     The router instance
 * @param client     The specific client data
 * @param avro_file  The requested AVRO file
 * @return True if more data needs to be read
 */
static bool avro_client_stream_data(AvroSession *client)
{
    bool read_more = false;
    Avro *router = client->router;

    if (strnlen(client->avro_binfile, 1))
    {
        char filename[PATH_MAX + 1];
        snprintf(filename, PATH_MAX, "%s/%s", router->avrodir.c_str(), client->avro_binfile);

        bool ok = true;

        if (client->file_handle == NULL &&
            (client->file_handle = maxavro_file_open(filename)) == NULL)
        {
            ok = false;
        }

        if (ok)
        {
            switch (client->format)
            {
            case AVRO_FORMAT_JSON:
                /** Currently only JSON format supports seeking to a GTID */
                if (client->requested_gtid &&
                    seek_to_index_pos(client, client->file_handle) &&
                    seek_to_gtid(client, client->file_handle))
                {
                    client->requested_gtid = false;
                }

                read_more = stream_json(client);
                break;

            case AVRO_FORMAT_AVRO:
                read_more = stream_binary(client);
                break;

            default:
                MXS_ERROR("Unexpected format: %d", client->format);
                break;
            }


            if (maxavro_get_error(client->file_handle) != MAXAVRO_ERR_NONE)
            {
                MXS_ERROR("Reading Avro file failed with error '%s'.",
                          maxavro_get_error_string(client->file_handle));
            }

            client->last_sent_pos = client->file_handle->records_read;
        }
    }
    else
    {
        fprintf(stderr, "No file specified\n");
        dcb_printf(client->dcb, "ERR avro file not specified");
    }

    return read_more;
}

GWBUF* read_avro_json_schema(const char *avrofile, const char* dir)
{
    GWBUF* rval = NULL;
    const char *suffix = strrchr(avrofile, '.');

    if (suffix)
    {
        char buffer[PATH_MAX + 1];
        snprintf(buffer, sizeof(buffer), "%s/%.*s.avsc", dir, (int)(suffix - avrofile),
                 avrofile);
        FILE *file = fopen(buffer, "rb");

        if (file)
        {
            int nread;
            while ((nread = fread(buffer, 1, sizeof(buffer) - 1, file)) > 0)
            {
                while (isspace(buffer[nread - 1]))
                {
                    nread--;
                }

                buffer[nread++] = '\n';

                GWBUF * newbuf = gwbuf_alloc_and_load(nread, buffer);

                if (newbuf)
                {
                    rval = gwbuf_append(rval, newbuf);
                }
            }

            fclose(file);
        }
        else
        {
            MXS_ERROR("Failed to open file '%s': %d, %s", buffer, errno,
                      mxs_strerror(errno));
        }
    }
    return rval;
}

GWBUF* read_avro_binary_schema(const char *avrofile, const char* dir)
{
    GWBUF* rval = NULL;
    char buffer[PATH_MAX + 1];
    snprintf(buffer, sizeof(buffer), "%s/%s", dir, avrofile);
    MAXAVRO_FILE *file = maxavro_file_open(buffer);

    if (file)
    {
        rval = maxavro_file_binary_header(file);
        maxavro_file_close(file);
    }
    else
    {
        MXS_ERROR("Failed to open file '%s'.", buffer);
    }

    return rval;
}

/**
 * Rotate to a new Avro file
 * @param client Avro client session
 * @param fullname Absolute path to the file to rotate to
 */
static void rotate_avro_file(AvroSession *client, char *fullname)
{
    char *filename = strrchr(fullname, '/') + 1;
    size_t len = strlen(filename);
    if (len > AVRO_MAX_FILENAME_LEN)
    {
        // TODO: This function is in need of a return value. It would
        // TODO: be better to abort if the name is too long and also
        // TODO: if the opening of the file fails.
        MXS_ERROR("Filename %s of length %lu is longer than maximum allowed "
                  "length %d. Trailing data will be cut.",
                  filename, len, AVRO_MAX_FILENAME_LEN);
        len = AVRO_MAX_FILENAME_LEN;
    }
    strncpy(client->avro_binfile, filename, len);
    client->avro_binfile[len] = 0;
    client->last_sent_pos = 0;

    maxavro_file_close(client->file_handle);

    if ((client->file_handle = maxavro_file_open(fullname)) == NULL)
    {
        MXS_ERROR("Failed to open file: %s", filename);
    }
    else
    {
        MXS_INFO("Rotated '%s'@'%s' to file: %s", client->dcb->user,
                 client->dcb->remote, fullname);
    }
}

/**
 * Print the name of the next Avro file
 * @param file Current filename
 * @param dir Directory where the files exist
 * @param dest Destination where the full path to the file is printed
 * @param len Size of @p dest
 */
static void print_next_filename(const char *file, const char *dir, char *dest, size_t len)
{
    char buffer[strlen(file) + 1];
    strcpy(buffer, file);
    char *ptr = strrchr(buffer, '.');

    if (ptr)
    {
        ptr--;
        while (ptr > buffer && *(ptr) != '.')
        {
            ptr--;
        }

        int filenum = strtol(ptr + 1, NULL, 10);
        *ptr = '\0';
        snprintf(dest, len, "%s/%s.%06d.avro",
                 dir, buffer, filenum + 1);
    }
}

/**
 * @brief The client callback for sending data
 *
 * @param dcb Client DCB
 * @param reason Why the callback was called
 * @param userdata Data provided when the callback was added
 * @return Always 0
 */
int avro_client_callback(DCB *dcb, DCB_REASON reason, void *userdata)
{
    if (reason == DCB_REASON_DRAINED)
    {
        AvroSession *client = (AvroSession*)userdata;

        spinlock_acquire(&client->catch_lock);
        if (client->cstate & AVRO_CS_BUSY)
        {
            spinlock_release(&client->catch_lock);
            return 0;
        }

        client->cstate |= AVRO_CS_BUSY;
        spinlock_release(&client->catch_lock);

        if (client->last_sent_pos == 0)
        {
            /** Send the schema of the current file */
            GWBUF *schema = NULL;

            switch (client->format)
            {
            case AVRO_FORMAT_JSON:
                schema = read_avro_json_schema(client->avro_binfile, client->router->avrodir.c_str());
                break;

            case AVRO_FORMAT_AVRO:
                schema = read_avro_binary_schema(client->avro_binfile, client->router->avrodir.c_str());
                break;

            default:
                MXS_ERROR("Unknown client format: %d", client->format);
            }

            if (schema)
            {
                client->dcb->func.write(client->dcb, schema);
            }
        }

        /** Stream the data to the client */
        bool read_more = avro_client_stream_data(client);

        char filename[PATH_MAX + 1];
        print_next_filename(client->avro_binfile, client->router->avrodir.c_str(),
                            filename, sizeof(filename));

        bool next_file;
        /** If the next file is available, send it to the client */
        if ((next_file = (access(filename, R_OK) == 0)))
        {
            rotate_avro_file(client, filename);
        }

        spinlock_acquire(&client->catch_lock);
        client->cstate &= ~AVRO_CS_BUSY;
        client->cstate |= AVRO_WAIT_DATA;

        if (next_file || read_more)
        {
#ifdef SS_DEBUG
            if (read_more)
            {
                MXS_DEBUG("Burst limit hit, need to read more data.");
            }
#endif
            avro_notify_client(client);
        }
        spinlock_release(&client->catch_lock);
    }

    return 0;
}

/**
 * @brief Notify a client that new data is available
 *
 * The client catch_lock must be held when calling this function.
 *
 * @param client Client to notify
 */
void avro_notify_client(AvroSession *client)
{
    /* Add fake event that will call the avro_client_callback() routine */
    poll_fake_write_event(client->dcb);
    client->cstate &= ~AVRO_WAIT_DATA;
}

// static
AvroSession* AvroSession::create(Avro* inst, MXS_SESSION* session)
{
    AvroSession* client = NULL;
    sqlite3* handle;
    char dbpath[PATH_MAX + 1];
    snprintf(dbpath, sizeof(dbpath), "/%s/%s", inst->avrodir.c_str(), avro_index_name);

    if (sqlite3_open_v2(dbpath, &handle,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
    {
        MXS_ERROR("Failed to open SQLite database '%s': %s", dbpath, sqlite3_errmsg(handle));
        sqlite3_close_v2(handle);
    }
    else if ((client = new (std::nothrow) AvroSession(inst, session, handle)) == NULL)
    {
        MXS_OOM();
        sqlite3_close_v2(handle);
    }
    else
    {
        atomic_add(&inst->stats.n_clients, 1);
    }

    return client;
}

AvroSession::AvroSession(Avro* instance, MXS_SESSION* session, sqlite3* handle):
    dcb(session->client_dcb),
    state(AVRO_CLIENT_UNREGISTERED),
    format(AVRO_FORMAT_UNDEFINED),
    uuid(NULL),
    catch_lock(SPINLOCK_INIT),
    router(instance),
    file_handle(NULL),
    last_sent_pos(0),
    connect_time(time(NULL)),
    avro_binfile{0},
    requested_gtid(false),
    cstate(0),
    sqlite_handle(handle)
{
}

AvroSession::~AvroSession()
{
    ss_debug(int prev_val = )atomic_add(&router->stats.n_clients, -1);
    ss_dassert(prev_val > 0);

    free(uuid);
    maxavro_file_close(file_handle);
    sqlite3_close_v2(sqlite_handle);
}
