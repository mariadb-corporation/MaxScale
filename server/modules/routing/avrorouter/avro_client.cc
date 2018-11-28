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
 * @file avro_client.c - contains code for the AVRO router to client communication
 */

#include "avrorouter.hh"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <maxavro.h>
#include <fstream>
#include <sstream>
#include <maxscale/service.hh>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxbase/atomic.h>
#include <maxscale/dcb.h>
#include <maxscale/log.h>
#include <maxscale/version.h>
#include <maxscale/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/utils.hh>

std::pair<std::string, std::string> get_avrofile_and_gtid(std::string file);

enum
{
    AVRO_CLIENT_UNREGISTERED,
    AVRO_CLIENT_REGISTERED,
    AVRO_CLIENT_REQUEST_DATA,
    AVRO_CLIENT_ERRORED,
};

int AvroSession::routeQuery(GWBUF* queue)
{
    int rval = 1;

    switch (state)
    {
    case AVRO_CLIENT_ERRORED:
        /* force disconnection */
        return 0;
        break;

    case AVRO_CLIENT_UNREGISTERED:
        if (do_registration(queue) == 0)
        {
            state = AVRO_CLIENT_ERRORED;
            dcb_printf(dcb, "ERR, code 12, msg: Registration failed\n");
            /* force disconnection */
            dcb_close(dcb);
            rval = 0;
        }
        else
        {
            /* Send OK ack to client */
            dcb_printf(dcb, "OK\n");

            state = AVRO_CLIENT_REGISTERED;
            MXS_INFO("%s: Client [%s] has completed REGISTRATION action",
                     dcb->service->name,
                     dcb->remote != NULL ? dcb->remote : "");
        }
        break;

    case AVRO_CLIENT_REGISTERED:
    case AVRO_CLIENT_REQUEST_DATA:
        if (state == AVRO_CLIENT_REGISTERED)
        {
            state = AVRO_CLIENT_REQUEST_DATA;
        }

        /* Process command from client */
        process_command(queue);

        break;

    default:
        state = AVRO_CLIENT_ERRORED;
        rval = 0;
        break;
    }

    gwbuf_free(queue);

    return rval;
}

/**
 * Handle client registration
 *
 * @param data Buffer with registration message
 *
 * @return 1 on successful registration, 0 on error
 */
int AvroSession::do_registration(GWBUF* data)
{
    const char reg_uuid[] = "REGISTER UUID=";
    const int reg_uuid_len = strlen(reg_uuid);
    int data_len = GWBUF_LENGTH(data) - reg_uuid_len;
    char* request = (char*)GWBUF_DATA(data);
    int ret = 0;

    if (strstr(request, reg_uuid) != NULL)
    {
        char* sep_ptr;
        int uuid_len = (data_len > CDC_UUID_LEN) ? CDC_UUID_LEN : data_len;
        /* 36 +1 */
        char client_uuid[uuid_len + 1];
        memcpy(client_uuid, request + reg_uuid_len, uuid_len);
        client_uuid[uuid_len] = '\0';

        if ((sep_ptr = strchr(client_uuid, ',')) != NULL)
        {
            *sep_ptr = '\0';
        }
        if ((sep_ptr = strchr(client_uuid + strlen(client_uuid), ' ')) != NULL)
        {
            *sep_ptr = '\0';
        }
        if ((sep_ptr = strchr(client_uuid, ' ')) != NULL)
        {
            *sep_ptr = '\0';
        }

        if (strlen(client_uuid) < static_cast<size_t>(uuid_len))
        {
            data_len -= (uuid_len - strlen(client_uuid));
        }

        uuid_len = strlen(client_uuid);

        uuid = client_uuid;

        if (data_len > 0)
        {
            /* Check for CDC request type */
            char* tmp_ptr = strstr(request + sizeof(reg_uuid) + uuid_len, "TYPE=");
            if (tmp_ptr)
            {
                if (memcmp(tmp_ptr + 5, "AVRO", 4) == 0)
                {
                    ret = 1;
                    state = AVRO_CLIENT_REGISTERED;
                    format = AVRO_FORMAT_AVRO;
                }
                else if (memcmp(tmp_ptr + 5, "JSON", 4) == 0)
                {
                    ret = 1;
                    state = AVRO_CLIENT_REGISTERED;
                    format = AVRO_FORMAT_JSON;
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
void extract_gtid_request(gtid_pos_t* gtid, const char* start, int len)
{
    const char* ptr = start;
    int read = 0;

    while (ptr < start + len)
    {
        if (!isdigit(*ptr))
        {
            ptr++;
        }
        else
        {
            char* end;
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
 * @brief Check if a file exists in a directory
 *
 * @param dir Directory where the file is searched
 * @param file File to search
 * @return True if file exists
 */
bool file_in_dir(const char* dir, const char* file)
{
    char path[PATH_MAX + 1];

    snprintf(path, sizeof(path), "%s/%s", dir, file);

    return access(path, F_OK) == 0;
}

/**
 * @brief The client callback for sending data
 *
 * @param dcb Client DCB
 * @param reason Why the callback was called
 * @param userdata Data provided when the callback was added
 * @return Always 0
 */
int avro_client_callback(DCB* dcb, DCB_REASON reason, void* userdata)
{
    if (reason == DCB_REASON_DRAINED)
    {
        AvroSession* client = static_cast<AvroSession*>(userdata);
        client->client_callback();
    }

    return 0;
}

/**
 * @brief Form the full Avro file name
 *
 * @param file_ptr Requested file
 * @param data_len Length of string pointed by @p file_ptr
 * @param dest Destination where the file name is stored. Must be at least
 * @p data_len + 1 bytes.
 */
std::pair<std::string, std::string> get_avrofile_and_gtid(std::string file)
{
    mxs::ltrim(file);
    auto pos = file.find_first_of(' ');
    std::string filename;
    std::string gtid;

    if (pos != std::string::npos)
    {
        // Client requests a specific GTID
        filename = file.substr(0, pos);
        gtid = file.substr(pos + 1);
    }
    else
    {
        filename = file;
    }

    auto first_dot = filename.find_first_of('.');
    auto last_dot = filename.find_last_of('.');

    if (first_dot != std::string::npos
        && last_dot != std::string::npos
        && first_dot != last_dot)
    {
        // Exact file version specified e.g. test.t1.000002
        filename += ".avro";
    }
    else
    {
        // No version specified, send first file
        filename += ".000001.avro";
    }

    return std::make_pair(filename, gtid);
}

/**
 * Process command from client
 *
 * @param data Buffer containing the command
 *
 */
void AvroSession::process_command(GWBUF* queue)
{
    const char req_data[] = "REQUEST-DATA";
    const size_t req_data_len = sizeof(req_data) - 1;
    size_t buflen = gwbuf_length(queue);
    uint8_t data[buflen + 1];
    gwbuf_copy_data(queue, 0, buflen, data);
    data[buflen] = '\0';
    char* command_ptr = strstr((char*)data, req_data);

    if (command_ptr != NULL)
    {
        char* file_ptr = command_ptr + req_data_len;
        int data_len = GWBUF_LENGTH(queue) - req_data_len;

        if (data_len > 1)
        {
            auto file_and_gtid = get_avrofile_and_gtid(file_ptr);

            if (!file_and_gtid.second.empty())
            {
                requested_gtid = true;
                extract_gtid_request(&gtid, file_and_gtid.second.c_str(), file_and_gtid.second.size());
                memcpy(&gtid_start, &gtid, sizeof(gtid_start));
            }

            avro_binfile = file_and_gtid.first;

            if (file_in_dir(router->avrodir.c_str(), avro_binfile.c_str()))
            {
                /* set callback routine for data sending */
                dcb_add_callback(dcb, DCB_REASON_DRAINED, avro_client_callback, this);

                /* Add fake event that will call the avro_client_callback() routine */
                poll_fake_write_event(dcb);
            }
            else
            {
                dcb_printf(dcb, "ERR NO-FILE File '%s' not found.\n", avro_binfile.c_str());
            }
        }
        else
        {
            dcb_printf(dcb, "ERR REQUEST-DATA with no data\n");
        }
    }
    else
    {
        const char err[] = "ERR: Unknown command\n";
        GWBUF* reply = gwbuf_alloc_and_load(sizeof(err), err);
        dcb->func.write(dcb, reply);
    }
}

static int send_row(DCB* dcb, json_t* row)
{
    char* json = json_dumps(row, JSON_PRESERVE_ORDER);
    size_t len = strlen(json);
    GWBUF* buf = gwbuf_alloc(len + 1);
    int rc = 0;

    if (json && buf)
    {
        uint8_t* data = GWBUF_DATA(buf);
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

void AvroSession::set_current_gtid(json_t* row)
{
    json_t* obj = json_object_get(row, avro_sequence);
    mxb_assert(json_is_integer(obj));
    gtid.seq = json_integer_value(obj);

    obj = json_object_get(row, avro_server_id);
    mxb_assert(json_is_integer(obj));
    gtid.server_id = json_integer_value(obj);

    obj = json_object_get(row, avro_domain);
    mxb_assert(json_is_integer(obj));
    gtid.domain = json_integer_value(obj);
}

/**
 * @brief Stream Avro data in JSON format
 *
 * @param file File to stream from
 * @param dcb DCB to stream to
 * @return True if more data is readable, false if all data was sent
 */
bool AvroSession::stream_json()
{
    int bytes = 0;

    do
    {
        json_t* row;
        int rc = 1;
        while (rc > 0 && (row = maxavro_record_read_json(file_handle)))
        {
            rc = send_row(dcb, row);
            set_current_gtid(row);
            json_decref(row);
        }
        bytes += file_handle->buffer_size;
    }
    while (maxavro_next_block(file_handle) && bytes < AVRO_DATA_BURST_SIZE);

    return bytes >= AVRO_DATA_BURST_SIZE;
}

/**
 * @brief Stream Avro data in native Avro format
 *
 * @param file File to stream from
 * @param dcb DCB to stream to
 * @return True if streaming was successful, false if an error occurred
 */
bool AvroSession::stream_binary()
{
    GWBUF* buffer;
    uint64_t bytes = 0;
    int rc = 1;

    while (rc > 0 && bytes < AVRO_DATA_BURST_SIZE)
    {
        bytes += file_handle->buffer_size;
        if ((buffer = maxavro_record_read_binary(file_handle)))
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

bool AvroSession::seek_to_gtid()
{
    bool seeking = true;

    do
    {
        json_t* row;
        while ((row = maxavro_record_read_json(file_handle)))
        {
            json_t* obj = json_object_get(row, avro_sequence);
            mxb_assert(json_is_integer(obj));
            uint64_t value = json_integer_value(obj);

            /** If a larger GTID is found, use that */
            if (value >= gtid.seq)
            {
                obj = json_object_get(row, avro_server_id);
                mxb_assert(json_is_integer(obj));
                value = json_integer_value(obj);

                if (value == gtid.server_id)
                {
                    obj = json_object_get(row, avro_domain);
                    mxb_assert(json_is_integer(obj));
                    value = json_integer_value(obj);

                    if (value == gtid.domain)
                    {
                        MXS_INFO("Found GTID %lu-%lu-%lu for %s@%s",
                                 gtid.domain,
                                 gtid.server_id,
                                 gtid.seq,
                                 dcb->user,
                                 dcb->remote);
                        seeking = false;
                    }
                }
            }

            /** We'll send the first found row immediately since we have already
             * read the row into memory */
            if (!seeking)
            {
                send_row(dcb, row);
            }

            json_decref(row);
        }
    }
    while (seeking && maxavro_next_block(file_handle));

    return !seeking;
}

/**
 * Print JSON output from selected AVRO file
 *
 * @return True if more data needs to be read
 */
bool AvroSession::stream_data()
{
    bool read_more = false;

    if (!avro_binfile.empty())
    {
        bool ok = true;
        std::string filename = router->avrodir + '/' + avro_binfile;

        if (!file_handle && !(file_handle = maxavro_file_open(filename.c_str())))
        {
            ok = false;
        }

        if (ok)
        {
            switch (format)
            {
            case AVRO_FORMAT_JSON:
                /** Currently only JSON format supports seeking to a GTID */
                if (requested_gtid && seek_to_gtid())
                {
                    requested_gtid = false;
                }

                read_more = stream_json();
                break;

            case AVRO_FORMAT_AVRO:
                read_more = stream_binary();
                break;

            default:
                MXS_ERROR("Unexpected format: %d", format);
                break;
            }


            if (maxavro_get_error(file_handle) != MAXAVRO_ERR_NONE)
            {
                MXS_ERROR("Reading Avro file failed with error '%s'.",
                          maxavro_get_error_string(file_handle));
            }

            last_sent_pos = file_handle->records_read;
        }
    }
    else
    {
        dcb_printf(dcb, "ERR avro file not specified\n");
    }

    return read_more;
}

GWBUF* read_avro_json_schema(std::string avrofile, std::string dir)
{
    GWBUF* rval = NULL;

    // Copy the name and swap the suffix from .avro to .avsc
    std::string schemafile = dir + "/" + avrofile.substr(0, avrofile.length() - 2) + "sc";
    std::ifstream file(schemafile);

    if (file.good())
    {
        std::stringstream ss;
        ss << file.rdbuf();
        std::string text = ss.str();
        mxs::Buffer buffer(std::vector<uint8_t>(text.begin(), text.end()));
        rval = buffer.release();
    }
    else
    {
        MXS_ERROR("Failed to open file '%s': %d, %s",
                  schemafile.c_str(),
                  errno,
                  mxs_strerror(errno));
    }

    return rval;
}

GWBUF* read_avro_binary_schema(std::string avrofile, std::string dir)
{
    GWBUF* rval = NULL;
    std::string filename = dir + '/' + avrofile;
    MAXAVRO_FILE* file = maxavro_file_open(filename.c_str());

    if (file)
    {
        rval = maxavro_file_binary_header(file);
        maxavro_file_close(file);
    }
    else
    {
        MXS_ERROR("Failed to open file '%s'.", filename.c_str());
    }

    return rval;
}

/**
 * Rotate to a new Avro file
 * @param client Avro client session
 * @param fullname Absolute path to the file to rotate to
 */
void AvroSession::rotate_avro_file(std::string fullname)
{
    auto pos = fullname.find_last_of('/');
    mxb_assert(pos != std::string::npos);
    avro_binfile = fullname.substr(pos + 1);
    last_sent_pos = 0;

    maxavro_file_close(file_handle);

    if ((file_handle = maxavro_file_open(fullname.c_str())) == NULL)
    {
        MXS_ERROR("Failed to open file: %s", fullname.c_str());
    }
    else
    {
        MXS_INFO("Rotated '%s'@'%s' to file: %s",
                 dcb->user,
                 dcb->remote,
                 fullname.c_str());
    }
}

/**
 * Print the name of the next Avro file
 * @param file Current filename
 * @param dir Directory where the files exist
 * @param dest Destination where the full path to the file is printed
 * @param len Size of @p dest
 */
static std::string get_next_filename(std::string file, std::string dir)
{
    // Find the last and second to last dot
    auto last = file.find_last_of('.');
    auto almost_last = file.find_last_of('.', last);
    mxb_assert(last != std::string::npos && almost_last != std::string::npos);

    // Extract the number between the dots
    std::string number_part = file.substr(almost_last + 1, last);
    int filenum = strtol(number_part.c_str(), NULL, 10);

    std::string file_part = file.substr(0, almost_last);

    // Print it out the new filename with the file number incremented by one
    char outbuf[PATH_MAX + 1];
    snprintf(outbuf,
             sizeof(outbuf),
             "%s/%s.%06d.avro",
             dir.c_str(),
             file_part.c_str(),
             filenum + 1);

    return std::string(outbuf);
}

void AvroSession::client_callback()
{
    if (last_sent_pos == 0)
    {
        // TODO: Don't use DCB callbacks to stream the data
        last_sent_pos = 1;

        /** Send the schema of the current file */
        GWBUF* schema = NULL;

        switch (format)
        {
        case AVRO_FORMAT_JSON:
            schema = read_avro_json_schema(avro_binfile, router->avrodir);
            break;

        case AVRO_FORMAT_AVRO:
            schema = read_avro_binary_schema(avro_binfile, router->avrodir);
            break;

        default:
            MXS_ERROR("Unknown client format: %d", format);
            break;
        }

        if (schema)
        {
            dcb->func.write(dcb, schema);
        }
    }

    /** Stream the data to the client */
    bool read_more = stream_data();
    std::string filename = get_next_filename(avro_binfile, router->avrodir);

    bool next_file;
    /** If the next file is available, send it to the client */
    if ((next_file = (access(filename.c_str(), R_OK) == 0)))
    {
        rotate_avro_file(filename);
    }

    if (next_file || read_more)
    {
        poll_fake_write_event(dcb);
    }
}

// static
AvroSession* AvroSession::create(Avro* inst, MXS_SESSION* session)
{
    return new(std::nothrow) AvroSession(inst, session);
}

AvroSession::AvroSession(Avro* instance, MXS_SESSION* session)
    : dcb(session->client_dcb)
    , state(AVRO_CLIENT_UNREGISTERED)
    , format(AVRO_FORMAT_UNDEFINED)
    , router(instance)
    , file_handle(NULL)
    , last_sent_pos(0)
    , connect_time(time(NULL))
    , requested_gtid(false)
{
}

AvroSession::~AvroSession()
{
    maxavro_file_close(file_handle);
}
