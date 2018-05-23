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
 * @file avro_index.c - GTID to file position index
 *
 * This file contains functions used to store index information
 * about GTID position in an Avro file. Since all records in the Avro file
 * that avrorouter uses contain the common GTID field, we can use it to create
 * an index. This can then be used to speed up retrieval of Avro records by
 * seeking to the offset of the file and reading the record instead of iterating
 * through all the records and looking for a matching record.
 *
 * The index is stored as an SQLite3 database.
 */

#include "avrorouter.hh"

#include <maxscale/debug.h>
#include <glob.h>

void* safe_key_free(void *data);

static const char insert_template[] = "INSERT INTO gtid(domain, server_id, "
                                      "sequence, avrofile, position) values (%lu, %lu, %lu, \"%s\", %ld);";

static void set_gtid(gtid_pos_t *gtid, json_t *row)
{
    json_t *obj = json_object_get(row, avro_sequence);
    ss_dassert(json_is_integer(obj));
    gtid->seq = json_integer_value(obj);

    obj = json_object_get(row, avro_server_id);
    ss_dassert(json_is_integer(obj));
    gtid->server_id = json_integer_value(obj);

    obj = json_object_get(row, avro_domain);
    ss_dassert(json_is_integer(obj));
    gtid->domain = json_integer_value(obj);
}

int index_query_cb(void *data, int rows, char** values, char** names)
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

void avro_index_file(Avro *router, const char* filename)
{
    MAXAVRO_FILE *file = maxavro_file_open(filename);

    if (file)
    {
        const char *name = strrchr(filename, '/');
        ss_dassert(name);

        if (name)
        {
            char sql[AVRO_SQL_BUFFER_SIZE];
            char *errmsg;
            long pos = -1;
            name++;

            snprintf(sql, sizeof(sql), "SELECT position FROM " INDEX_TABLE_NAME
                     " WHERE filename=\"%s\";", name);

            if (sqlite3_exec(router->sqlite_handle, sql, index_query_cb, &pos, &errmsg) != SQLITE_OK)
            {
                MXS_ERROR("Failed to read last indexed position of file '%s': %s",
                          name, errmsg);
                sqlite3_free(errmsg);
                maxavro_file_close(file);
                return;
            }

            /** Continue from last position */
            if (pos > 0 && !maxavro_record_set_pos(file, pos))
            {
                maxavro_file_close(file);
                return;
            }

            gtid_pos_t prev_gtid;

            if (sqlite3_exec(router->sqlite_handle, "BEGIN", NULL, NULL, &errmsg) != SQLITE_OK)
            {
                MXS_ERROR("Failed to start transaction: %s", errmsg);
            }
            sqlite3_free(errmsg);

            do
            {
                json_t *row = maxavro_record_read_json(file);

                if (row)
                {
                    gtid_pos_t gtid;
                    set_gtid(&gtid, row);

                    if (prev_gtid.domain != gtid.domain ||
                        prev_gtid.server_id != gtid.server_id ||
                        prev_gtid.seq != gtid.seq)
                    {
                        snprintf(sql, sizeof(sql), insert_template, gtid.domain,
                                 gtid.server_id, gtid.seq, name, file->block_start_pos);
                        if (sqlite3_exec(router->sqlite_handle, sql, NULL, NULL,
                                         &errmsg) != SQLITE_OK)
                        {
                            MXS_ERROR("Failed to insert GTID %lu-%lu-%lu for %s "
                                      "into index database: %s", gtid.domain,
                                      gtid.server_id, gtid.seq, name, errmsg);

                        }
                        sqlite3_free(errmsg);
                        errmsg = NULL;
                        prev_gtid = gtid;
                    }
                    json_decref(row);
                }
                else
                {
                    break;
                }
            }
            while (maxavro_next_block(file));

            if (sqlite3_exec(router->sqlite_handle, "COMMIT", NULL, NULL, &errmsg) != SQLITE_OK)
            {
                MXS_ERROR("Failed to commit transaction: %s", errmsg);
            }
            sqlite3_free(errmsg);

            snprintf(sql, sizeof(sql), "INSERT OR REPLACE INTO " INDEX_TABLE_NAME
                     " values (%lu, \"%s\");", file->block_start_pos, name);
            if (sqlite3_exec(router->sqlite_handle, sql, NULL, NULL,
                             &errmsg) != SQLITE_OK)
            {
                MXS_ERROR("Failed to update indexing progress: %s", errmsg);
            }
            sqlite3_free(errmsg);
            errmsg = NULL;
        }
        else
        {
            MXS_ERROR("Malformed filename: %s", filename);
        }

        maxavro_file_close(file);
    }
    else
    {
        MXS_ERROR("Failed to open file '%s' when generating file index.", filename);
    }
}

/**
 * @brief Avro file indexing task
 *
 * Builds an index of filenames, GTIDs and positions in the Avro file.
 * This allows all tables that contain a GTID to be fetched in an effiecent
 * manner.
 * @param data The router instance
 */
void avro_update_index(Avro* router)
{
    char path[PATH_MAX + 1];
    snprintf(path, sizeof(path), "%s/*.avro", router->avrodir.c_str());
    glob_t files;

    if (glob(path, 0, NULL, &files) != GLOB_NOMATCH)
    {
        for (size_t i = 0; i < files.gl_pathc; i++)
        {
            avro_index_file(router, files.gl_pathv[i]);
        }
    }

    globfree(&files);
}

/** The SQL for the in-memory used_tables table */
static const char *insert_sql = "INSERT OR IGNORE INTO " MEMORY_TABLE_NAME
                                "(domain, server_id, sequence, binlog_timestamp, table_name)"
                                " VALUES (%lu, %lu, %lu, %u, \"%s\")";

/**
 * @brief Add a used table to the current transaction
 *
 * This adds a table to the in-memory table used to store tables used by
 * transactions. These are later flushed to disk with the Avro records.
 *
 * @param router Avro router instance
 * @param table Table to add
 */
void add_used_table(Avro* router, const char* table)
{
    char sql[AVRO_SQL_BUFFER_SIZE], *errmsg;
    snprintf(sql, sizeof(sql), insert_sql, router->gtid.domain, router->gtid.server_id,
             router->gtid.seq, router->gtid.timestamp, table);

    if (sqlite3_exec(router->sqlite_handle, sql, NULL, NULL, &errmsg) != SQLITE_OK)
    {
        MXS_ERROR("Failed to add used table %s for GTID %lu-%lu-%lu: %s",
                  table, router->gtid.domain, router->gtid.server_id,
                  router->gtid.seq, errmsg);
    }
    sqlite3_free(errmsg);
}

/**
 * @brief Update the tables used in a transaction
 *
 * This flushes the in-memory table to disk and should be called after the
 * Avro records have been flushed to disk.
 *
 * @param router Avro router instance
 */
void update_used_tables(Avro* router)
{
    char *errmsg;

    if (sqlite3_exec(router->sqlite_handle, "INSERT INTO " USED_TABLES_TABLE_NAME
                     " SELECT * FROM " MEMORY_TABLE_NAME, NULL, NULL, &errmsg) != SQLITE_OK)
    {
        MXS_ERROR("Failed to transfer used table data from memory to disk: %s", errmsg);
    }
    sqlite3_free(errmsg);

    if (sqlite3_exec(router->sqlite_handle, "DELETE FROM " MEMORY_TABLE_NAME,
                     NULL, NULL, &errmsg) != SQLITE_OK)
    {
        MXS_ERROR("Failed to transfer used table data from memory to disk: %s", errmsg);
    }
    sqlite3_free(errmsg);
}
