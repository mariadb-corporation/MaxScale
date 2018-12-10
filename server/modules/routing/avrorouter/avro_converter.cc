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

#include "avro_converter.hh"

#include <limits.h>

#include <maxbase/assert.h>
#include <maxscale/alloc.h>
#include <maxscale/log.hh>

/**
 * @brief Allocate an Avro table
 *
 * Create an Aro table and prepare it for writing.
 * @param filepath Path to the created file
 * @param json_schema The schema of the table in JSON format
 */
AvroTable* avro_table_alloc(const char* filepath,
                            const char* json_schema,
                            const char* codec,
                            size_t block_size)
{
    avro_file_writer_t avro_file;
    avro_value_iface_t* avro_writer_iface;
    avro_schema_t avro_schema;

    if (avro_schema_from_json_length(json_schema,
                                     strlen(json_schema),
                                     &avro_schema))
    {
        MXS_ERROR("Avro error: %s", avro_strerror());
        MXS_INFO("Avro schema: %s", json_schema);
        return NULL;
    }

    int rc = 0;

    if (access(filepath, F_OK) == 0)
    {
        rc = avro_file_writer_open_bs(filepath, &avro_file, block_size);
    }
    else
    {
        rc = avro_file_writer_create_with_codec(filepath,
                                                avro_schema,
                                                &avro_file,
                                                codec,
                                                block_size);
    }

    if (rc)
    {
        MXS_ERROR("Avro error: %s", avro_strerror());
        avro_schema_decref(avro_schema);
        return NULL;
    }

    if ((avro_writer_iface = avro_generic_class_from_schema(avro_schema)) == NULL)
    {
        MXS_ERROR("Avro error: %s", avro_strerror());
        avro_schema_decref(avro_schema);
        avro_file_writer_close(avro_file);
        return NULL;
    }

    AvroTable* table = new(std::nothrow) AvroTable(avro_file, avro_writer_iface, avro_schema);

    if (!table)
    {
        avro_file_writer_close(avro_file);
        avro_value_iface_decref(avro_writer_iface);
        avro_schema_decref(avro_schema);
        MXS_OOM();
    }

    return table;
}

/**
 * @brief Convert the MySQL column type to a compatible Avro type
 *
 * Some fields are larger than they need to be but since the Avro integer
 * compression is quite efficient, the real loss in performance is negligible.
 * @param type MySQL column type
 * @return String representation of the Avro type
 */
static const char* column_type_to_avro_type(uint8_t type)
{
    switch (type)
    {
    case TABLE_COL_TYPE_TINY:
    case TABLE_COL_TYPE_SHORT:
    case TABLE_COL_TYPE_LONG:
    case TABLE_COL_TYPE_INT24:
    case TABLE_COL_TYPE_BIT:
        return "int";

    case TABLE_COL_TYPE_FLOAT:
        return "float";

    case TABLE_COL_TYPE_DOUBLE:
    case TABLE_COL_TYPE_NEWDECIMAL:
        return "double";

    case TABLE_COL_TYPE_NULL:
        return "null";

    case TABLE_COL_TYPE_LONGLONG:
        return "long";

    case TABLE_COL_TYPE_TINY_BLOB:
    case TABLE_COL_TYPE_MEDIUM_BLOB:
    case TABLE_COL_TYPE_LONG_BLOB:
    case TABLE_COL_TYPE_BLOB:
        return "bytes";

    default:
        return "string";
    }
}

/**
 * @brief Create a new JSON Avro schema from the table map and create table abstractions
 *
 * The schema will always have a GTID field and all records contain the current
 * GTID of the transaction.
 * @param map TABLE_MAP for this table
 * @param create The TABLE_CREATE for this table
 * @return New schema or NULL if an error occurred
 */
char* json_new_schema_from_table(const STableMapEvent& map, const STableCreateEvent& create)
{
    if (map->version != create->version)
    {
        MXS_ERROR("Version mismatch for table %s.%s. Table map version is %d and "
                  "the table definition version is %d.",
                  map->database.c_str(),
                  map->table.c_str(),
                  map->version,
                  create->version);
        mxb_assert(!true);      // Should not happen
        return NULL;
    }

    json_error_t err;
    memset(&err, 0, sizeof(err));
    json_t* schema = json_object();
    json_object_set_new(schema, "namespace", json_string("MaxScaleChangeDataSchema.avro"));
    json_object_set_new(schema, "type", json_string("record"));
    json_object_set_new(schema, "name", json_string("ChangeRecord"));

    json_t* array = json_array();
    json_array_append_new(array,
                          json_pack_ex(&err,
                                       0,
                                       "{s:s, s:s}",
                                       "name",
                                       avro_domain,
                                       "type",
                                       "int"));
    json_array_append_new(array,
                          json_pack_ex(&err,
                                       0,
                                       "{s:s, s:s}",
                                       "name",
                                       avro_server_id,
                                       "type",
                                       "int"));
    json_array_append_new(array,
                          json_pack_ex(&err,
                                       0,
                                       "{s:s, s:s}",
                                       "name",
                                       avro_sequence,
                                       "type",
                                       "int"));
    json_array_append_new(array,
                          json_pack_ex(&err,
                                       0,
                                       "{s:s, s:s}",
                                       "name",
                                       avro_event_number,
                                       "type",
                                       "int"));
    json_array_append_new(array,
                          json_pack_ex(&err,
                                       0,
                                       "{s:s, s:s}",
                                       "name",
                                       avro_timestamp,
                                       "type",
                                       "int"));

    /** Enums and other complex types are defined with complete JSON objects
     * instead of string values */
    json_t* event_types = json_pack_ex(&err,
                                       0,
                                       "{s:s, s:s, s:[s,s,s,s]}",
                                       "type",
                                       "enum",
                                       "name",
                                       "EVENT_TYPES",
                                       "symbols",
                                       "insert",
                                       "update_before",
                                       "update_after",
                                       "delete");

    // Ownership of `event_types` is stolen when using the `o` format
    json_array_append_new(array,
                          json_pack_ex(&err,
                                       0,
                                       "{s:s, s:o}",
                                       "name",
                                       avro_event_type,
                                       "type",
                                       event_types));

    for (uint64_t i = 0; i < map->columns() && i < create->columns.size(); i++)
    {
        json_array_append_new(array,
                              json_pack_ex(&err,
                                           0,
                                           "{s:s, s:[s, s], s:s, s:i}",
                                           "name",
                                           create->columns[i].name.c_str(),
                                           "type",
                                           "null",
                                           column_type_to_avro_type(map->column_types[i]),
                                           "real_type",
                                           create->columns[i].type.c_str(),
                                           "length",
                                           create->columns[i].length));
    }
    json_object_set_new(schema, "fields", array);
    char* rval = json_dumps(schema, JSON_PRESERVE_ORDER);
    json_decref(schema);
    return rval;
}

/**
 * @brief Save the Avro schema of a table to disk
 *
 * @param path Schema directory
 * @param schema Schema in JSON format
 * @param map Table map that @p schema represents
 */
void save_avro_schema(const char* path,
                      const char* schema,
                      const STableMapEvent& map,
                      const STableCreateEvent& create)
{
    char filepath[PATH_MAX];
    snprintf(filepath,
             sizeof(filepath),
             "%s/%s.%s.%06d.avsc",
             path,
             map->database.c_str(),
             map->table.c_str(),
             map->version);

    if (access(filepath, F_OK) != 0)
    {
        if (!create->was_used)
        {
            FILE* file = fopen(filepath, "wb");
            if (file)
            {
                fprintf(file, "%s\n", schema);
                fclose(file);
            }
        }
    }
}

static const char* codec_to_string(enum mxs_avro_codec_type type)
{
    switch (type)
    {
    case MXS_AVRO_CODEC_NULL:
        return "null";

    case MXS_AVRO_CODEC_DEFLATE:
        return "deflate";

    case MXS_AVRO_CODEC_SNAPPY:
        return "snappy";

    default:
        mxb_assert(false);
        return "null";
    }
}

AvroConverter::AvroConverter(std::string avrodir, uint64_t block_size, mxs_avro_codec_type codec)
    : m_avrodir(avrodir)
    , m_block_size(block_size)
    , m_codec(codec)
{
}

bool AvroConverter::open_table(const STableMapEvent& map, const STableCreateEvent& create)
{
    bool rval = false;
    char* json_schema = json_new_schema_from_table(map, create);

    if (json_schema)
    {
        char filepath[PATH_MAX + 1];
        snprintf(filepath,
                 sizeof(filepath),
                 "%s/%s.%s.%06d.avro",
                 m_avrodir.c_str(),
                 map->database.c_str(),
                 map->table.c_str(),
                 map->version);

        SAvroTable avro_table(avro_table_alloc(filepath,
                                               json_schema,
                                               codec_to_string(m_codec),
                                               m_block_size));

        if (avro_table)
        {
            m_open_tables[map->database + "." + map->table] = avro_table;
            save_avro_schema(m_avrodir.c_str(), json_schema, map, create);
            m_map = map;
            m_create = create;
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to open new Avro file for writing.");
        }
        MXS_FREE(json_schema);
    }
    else
    {
        MXS_ERROR("Failed to create JSON schema.");
    }

    return rval;
}

bool AvroConverter::prepare_table(const STableMapEvent& map, const STableCreateEvent& create)
{
    bool rval = false;
    auto it = m_open_tables.find(map->database + "." + map->table);

    if (it != m_open_tables.end())
    {
        m_writer_iface = it->second->avro_writer_iface;
        m_avro_file = &it->second->avro_file;
        m_map = map;
        m_create = create;
        rval = true;
    }

    return rval;
}

void AvroConverter::flush_tables()
{
    for (auto it = m_open_tables.begin(); it != m_open_tables.end(); it++)
    {
        avro_file_writer_flush(it->second->avro_file);
    }
}

void AvroConverter::prepare_row(const gtid_pos_t& gtid, const REP_HEADER& hdr, int event_type)
{
    avro_generic_value_new(m_writer_iface, &m_record);
    avro_value_get_by_name(&m_record, avro_domain, &m_field, NULL);
    avro_value_set_int(&m_field, gtid.domain);

    avro_value_get_by_name(&m_record, avro_server_id, &m_field, NULL);
    avro_value_set_int(&m_field, gtid.server_id);

    avro_value_get_by_name(&m_record, avro_sequence, &m_field, NULL);
    avro_value_set_int(&m_field, gtid.seq);

    avro_value_get_by_name(&m_record, avro_event_number, &m_field, NULL);
    avro_value_set_int(&m_field, gtid.event_num);

    avro_value_get_by_name(&m_record, avro_timestamp, &m_field, NULL);
    avro_value_set_int(&m_field, hdr.timestamp);

    avro_value_get_by_name(&m_record, avro_event_type, &m_field, NULL);
    avro_value_set_enum(&m_field, event_type);
}

bool AvroConverter::commit(const gtid_pos_t& gtid)
{
    bool rval = true;

    if (avro_file_writer_append_value(*m_avro_file, &m_record))
    {
        MXS_ERROR("Failed to write value: %s", avro_strerror());
        rval = false;
    }

    return rval;
}

void AvroConverter::column(int i, int32_t value)
{
    set_active(i);
    avro_value_set_int(&m_field, value);
}

void AvroConverter::column(int i, int64_t value)
{
    set_active(i);
    avro_value_set_long(&m_field, value);
}

void AvroConverter::column(int i, float value)
{
    set_active(i);
    avro_value_set_float(&m_field, value);
}

void AvroConverter::column(int i, double value)
{
    set_active(i);
    avro_value_set_double(&m_field, value);
}

void AvroConverter::column(int i, std::string value)
{
    set_active(i);
    avro_value_set_string(&m_field, value.c_str());
}

void AvroConverter::column(int i, uint8_t* value, int len)
{
    set_active(i);
    avro_value_set_bytes(&m_field, value, len);
}

void AvroConverter::column(int i)
{
    set_active(i);
    avro_value_set_branch(&m_union_value, 0, &m_field);
    avro_value_set_null(&m_field);
}

void AvroConverter::set_active(int i)
{
    MXB_AT_DEBUG(int rc = ) avro_value_get_by_name(&m_record,
                                                   m_create->columns[i].name.c_str(),
                                                   &m_union_value,
                                                   NULL);
    mxb_assert(rc == 0);
    avro_value_set_branch(&m_union_value, 1, &m_field);
}
