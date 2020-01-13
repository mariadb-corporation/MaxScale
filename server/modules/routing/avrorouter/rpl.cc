/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-01-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "avrorouter.hh"
#include "rpl.hh"

#include <sstream>
#include <algorithm>

#include <glob.h>

#include <maxbase/assert.h>
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

constexpr int WRITE_EVENT = 0;
constexpr int UPDATE_EVENT = 1;
constexpr int UPDATE_EVENT_AFTER = 2;
constexpr int DELETE_EVENT = 3;

namespace
{

static bool warn_bit = false;           /**< Remove when support for BIT is added */

/**
 * @brief Get row event name
 * @param event Event type
 * @return String representation of the event
 */
int get_event_type(uint8_t event)
{
    switch (event)
    {

    case WRITE_ROWS_EVENTv0:
    case WRITE_ROWS_EVENTv1:
    case WRITE_ROWS_EVENTv2:
        return WRITE_EVENT;

    case UPDATE_ROWS_EVENTv0:
    case UPDATE_ROWS_EVENTv1:
    case UPDATE_ROWS_EVENTv2:
        return UPDATE_EVENT;

    case DELETE_ROWS_EVENTv0:
    case DELETE_ROWS_EVENTv1:
    case DELETE_ROWS_EVENTv2:
        return DELETE_EVENT;

    default:
        MXS_ERROR("Unexpected event type: %d (%0x)", event, event);
        return -1;
    }
}

/**
 * @brief Unpack numeric types
 *
 * Convert the raw binary data into actual numeric types.
 *
 * @param conv     Event converter to use
 * @param idx      Position of this column in the row
 * @param type     Event type
 * @param metadata Field metadata
 * @param value    Pointer to the start of the in-memory representation of the data
 */
void set_numeric_field_value(SRowEventHandler& conv,
                             int idx,
                             uint8_t type,
                             uint8_t* metadata,
                             uint8_t* value,
                             bool is_unsigned)
{
    switch (type)
    {
    case TABLE_COL_TYPE_TINY:
        if (is_unsigned)
        {
            uint8_t c = *value;
            conv->column_int(idx, c);
        }
        else
        {
            int8_t c = *value;
            conv->column_int(idx, c);
        }
        break;

    case TABLE_COL_TYPE_SHORT:
        if (is_unsigned)
        {
            uint16_t s = gw_mysql_get_byte2(value);
            conv->column_int(idx, s);
        }
        else
        {
            int16_t s = gw_mysql_get_byte2(value);
            conv->column_int(idx, s);
        }
        break;

    case TABLE_COL_TYPE_INT24:
        if (is_unsigned)
        {
            uint32_t x = gw_mysql_get_byte3(value);
            conv->column_int(idx, x);
        }
        else
        {
            int32_t x = gw_mysql_get_byte3(value);

            if (x & 0x800000)
            {
                x = -((0xffffff & (~x)) + 1);
            }

            conv->column_int(idx, (int64_t)x);
        }
        break;

    case TABLE_COL_TYPE_LONG:
        if (is_unsigned)
        {
            uint32_t x = gw_mysql_get_byte4(value);
            conv->column_long(idx, x);
        }
        else
        {
            int32_t x = gw_mysql_get_byte4(value);
            conv->column_long(idx, x);
        }
        break;

    case TABLE_COL_TYPE_LONGLONG:
        conv->column_long(idx, gw_mysql_get_byte8(value));
        break;

    case TABLE_COL_TYPE_FLOAT:
        {
            float f = 0;
            memcpy(&f, value, 4);
            conv->column_float(idx, f);
            break;
        }

    case TABLE_COL_TYPE_DOUBLE:
        {
            double d = 0;
            memcpy(&d, value, 8);
            conv->column_double(idx, d);
            break;
        }

    default:
        break;
    }
}

/**
 * @brief Check if a bit is set
 *
 * @param ptr Pointer to start of bitfield
 * @param columns Number of columns (bits)
 * @param current_column Zero indexed column number
 * @return True if the bit is set
 */
bool bit_is_set(uint8_t* ptr, int columns, int current_column)
{
    if (current_column >= 8)
    {
        ptr += current_column / 8;
        current_column = current_column % 8;
    }

    return (*ptr) & (1 << current_column);
}

/**
 * @brief Get the length of the metadata for a particular field
 *
 * @param type Type of the field
 * @return Length of the metadata for this field
 */
int get_metadata_len(uint8_t type)
{
    switch (type)
    {
    case TABLE_COL_TYPE_STRING:
    case TABLE_COL_TYPE_VAR_STRING:
    case TABLE_COL_TYPE_VARCHAR:
    case TABLE_COL_TYPE_DECIMAL:
    case TABLE_COL_TYPE_NEWDECIMAL:
    case TABLE_COL_TYPE_ENUM:
    case TABLE_COL_TYPE_SET:
    case TABLE_COL_TYPE_BIT:
        return 2;

    case TABLE_COL_TYPE_BLOB:
    case TABLE_COL_TYPE_FLOAT:
    case TABLE_COL_TYPE_DOUBLE:
    case TABLE_COL_TYPE_DATETIME2:
    case TABLE_COL_TYPE_TIMESTAMP2:
    case TABLE_COL_TYPE_TIME2:
        return 1;

    default:
        return 0;
    }
}

// Make sure that both `i` and `trace` are defined before using this macro
#define check_overflow(t) \
    do \
    { \
        if (!(t)) \
        { \
            for (long x = 0; x < i; x++) \
            { \
                MXS_ALERT("%s", trace[x]); \
            } \
            raise(SIGABRT); \
        } \
    } while (false)

// Debug function for checking whether a row event consists of only NULL values
bool all_fields_null(uint8_t* null_bitmap, int ncolumns)
{
    bool rval = true;

    for (long i = 0; i < ncolumns; i++)
    {
        if (!bit_is_set(null_bitmap, ncolumns, i))
        {
            rval = false;
            break;
        }
    }

    return rval;
}

/**
 * @brief Read the fully qualified name of the table
 *
 * @param ptr Pointer to the start of the event payload
 * @param post_header_len Length of the event specific header, 8 or 6 bytes
 * @param dest Destination where the string is stored
 * @param len Size of destination
 */
void read_table_info(uint8_t* ptr, uint8_t post_header_len, uint64_t* tbl_id, char* dest, size_t len)
{
    uint64_t table_id = 0;
    size_t id_size = post_header_len == 6 ? 4 : 6;
    memcpy(&table_id, ptr, id_size);
    ptr += id_size;

    uint16_t flags = 0;
    memcpy(&flags, ptr, 2);
    ptr += 2;

    uint8_t schema_name_len = *ptr++;
    char schema_name[schema_name_len + 2];

    /** Copy the NULL byte after the schema name */
    memcpy(schema_name, ptr, schema_name_len + 1);
    ptr += schema_name_len + 1;

    uint8_t table_name_len = *ptr++;
    char table_name[table_name_len + 2];

    /** Copy the NULL byte after the table name */
    memcpy(table_name, ptr, table_name_len + 1);

    snprintf(dest, len, "%s.%s", schema_name, table_name);
    *tbl_id = table_id;
}

void normalize_sql_string(std::string& str)
{
    // remove mysql comments
    const char* remove_comments_pattern =
        "(?:`[^`]*`\\K)|"
        "(\\/[*](?!(M?!)).*?[*]\\/)|"
        "((?:#.*|--[[:space:]].*)(\\n|\\r\\n|$))";

    str = mxb::Regex(remove_comments_pattern, PCRE2_SUBSTITUTE_GLOBAL).replace(str, "");

    // unify whitespace
    for (auto& a : str)
    {
        if (isspace(a) && a != ' ')
        {
            a = ' ';
        }
    }

    // strip executable comments
    if (strncmp(str.c_str(), "/*!", 3) == 0 || strncmp(str.c_str(), "/*M!", 4) == 0)
    {
        str.erase(0, 3);

        if (str.front() == '!')
        {
            str.erase(0, 1);
        }

        // Skip the versioning part
        while (!str.empty() && isdigit(str.front()))
        {
            str.erase(0, 1);
        }
    }
}

/**
 * @brief Check whether the field is one that was generated by the avrorouter
 *
 * @param name Name of the field in the Avro schema
 * @return True if field was not generated by the avrorouter
 */
static inline bool not_generated_field(const char* name)
{
    return strcmp(name, avro_domain) && strcmp(name, avro_server_id)
           && strcmp(name, avro_sequence) && strcmp(name, avro_event_number)
           && strcmp(name, avro_event_type) && strcmp(name, avro_timestamp);
}

/**
 * @brief Extract the field names from a JSON Avro schema file
 *
 * This function extracts the names of the columns from the JSON format Avro
 * schema in the file @c filename. This function assumes that the field definitions
 * in @c filename are in the same order as they are in the CREATE TABLE statement.
 *
 * @param filename The Avro schema in JSON format
 * @param table The TABLE_CREATE object to populate
 * @return True on success successfully, false on error
 */
bool json_extract_field_names(const char* filename, std::vector<Column>& columns)
{
    bool rval = false;
    json_error_t err;
    err.text[0] = '\0';
    json_t* obj;
    json_t* arr = nullptr;

    if ((obj = json_load_file(filename, 0, &err)) && (arr = json_object_get(obj, "fields")))
    {
        if (json_is_array(arr))
        {
            int array_size = json_array_size(arr);
            rval = true;

            for (int i = 0; i < array_size; i++)
            {
                json_t* val = json_array_get(arr, i);

                if (json_is_object(val))
                {
                    json_t* name = json_object_get(val, "name");

                    if (name && json_is_string(name))
                    {
                        const char* name_str = json_string_value(name);

                        if (not_generated_field(name_str))
                        {
                            columns.emplace_back(name_str);

                            json_t* value;

                            if ((value = json_object_get(val, "real_type")) && json_is_string(value))
                            {
                                columns.back().type = json_string_value(value);
                            }
                            else
                            {
                                MXS_WARNING("No \"real_type\" value defined. Treating as unknown type field.");
                            }

                            if ((value = json_object_get(val, "length")) && json_is_integer(value))
                            {
                                columns.back().length = json_integer_value(value);
                            }
                            else
                            {
                                MXS_WARNING("No \"length\" value defined. Treating as default length field.");
                            }

                            if ((value = json_object_get(val, "unsigned")) && json_is_boolean(value))
                            {
                                columns.back().is_unsigned = json_boolean_value(value);
                            }
                        }
                    }
                    else
                    {
                        MXS_ERROR("JSON value for \"name\" was not a string in "
                                  "file '%s'.",
                                  filename);
                        rval = false;
                    }
                }
                else
                {
                    MXS_ERROR("JSON value for \"fields\" was not an array of objects in "
                              "file '%s'.",
                              filename);
                    rval = false;
                }
            }
        }
        else
        {
            MXS_ERROR("JSON value for \"fields\" was not an array in file '%s'.", filename);
        }
        json_decref(obj);
    }
    else
    {
        MXS_ERROR("Failed to load JSON from file '%s': %s",
                  filename,
                  obj && !arr ? "No 'fields' value in object." : err.text);
    }

    return rval;
}

STable load_table_from_schema(const char* file, const char* db, const char* table, int version)
{
    STable rval;
    std::vector<Column> columns;

    if (json_extract_field_names(file, columns))
    {
        rval.reset(new Table(db, table, version, std::move(columns)));
    }

    return rval;
}
}

void gtid_pos_t::extract(const REP_HEADER& hdr, uint8_t* ptr)
{
    domain = extract_field(ptr + 8, 32);
    server_id = hdr.serverid;
    seq = extract_field(ptr, 64);
    event_num = 0;
    timestamp = hdr.timestamp;
}

bool gtid_pos_t::parse(const char* str)
{
    bool rval = false;
    char buf[strlen(str) + 1];
    strcpy(buf, str);
    char* saved, * dom = strtok_r(buf, ":-\n", &saved);
    char* serv_id = strtok_r(NULL, ":-\n", &saved);
    char* sequence = strtok_r(NULL, ":-\n", &saved);
    char* subseq = strtok_r(NULL, ":-\n", &saved);

    if (dom && serv_id && sequence)
    {
        domain = strtol(dom, NULL, 10);
        server_id = strtol(serv_id, NULL, 10);
        seq = strtol(sequence, NULL, 10);
        event_num = subseq ? strtol(subseq, NULL, 10) : 0;
        rval = true;
    }

    return rval;
}

gtid_pos_t gtid_pos_t::from_string(std::string str)
{
    gtid_pos_t gtid;
    gtid.parse(str.c_str());
    return gtid;
}

std::string gtid_pos_t::to_string() const
{
    std::stringstream ss;
    ss << domain << "-" << server_id << "-" << seq;
    return ss.str();
}

bool gtid_pos_t::empty() const
{
    return timestamp == 0 && domain == 0 && server_id == 0 && seq == 0 && event_num == 0;
}

/**
 * Extract the field type and metadata information from the table map event
 *
 * @param ptr     Pointer to the start of the event payload
 * @param hdr_len Length of the event specific header, 8 or 6 bytes
 *
 * @return The ID the table was mapped to
 */
uint64_t Table::map_table(uint8_t* ptr, uint8_t hdr_len)
{
    uint64_t table_id = 0;
    size_t id_size = hdr_len == 6 ? 4 : 6;
    memcpy(&table_id, ptr, id_size);
    ptr += id_size;

    uint16_t flags = 0;
    memcpy(&flags, ptr, 2);
    ptr += 2;

    uint8_t schema_name_len = *ptr++;
    char schema_name[schema_name_len + 2];

    /** Copy the NULL byte after the schema name */
    memcpy(schema_name, ptr, schema_name_len + 1);
    ptr += schema_name_len + 1;

    uint8_t table_name_len = *ptr++;
    char table_name[table_name_len + 2];

    /** Copy the NULL byte after the table name */
    memcpy(table_name, ptr, table_name_len + 1);
    ptr += table_name_len + 1;

    uint64_t column_count = mxq::leint_value(ptr);
    ptr += mxq::leint_bytes(ptr);

    /** Column types */
    column_types.assign(ptr, ptr + column_count);
    ptr += column_count;

    size_t metadata_size = 0;
    uint8_t* metadata = (uint8_t*) mxq::lestr_consume(&ptr, &metadata_size);
    column_metadata.assign(metadata, metadata + metadata_size);

    size_t nullmap_size = (column_count + 7) / 8;
    null_bitmap.assign(ptr, ptr + nullmap_size);

    return table_id;
}

Rpl::Rpl(SERVICE* service,
         SRowEventHandler handler,
         pcre2_code* match,
         pcre2_code* exclude,
         gtid_pos_t gtid)
    : m_handler(std::move(handler))
    , m_service(service)
    , m_binlog_checksum(0)
    , m_event_types(0)
    , m_gtid(gtid)
    , m_match(match)
    , m_exclude(exclude)
    , m_md_match(m_match ? pcre2_match_data_create_from_pattern(m_match, NULL) : nullptr)
    , m_md_exclude(m_exclude ? pcre2_match_data_create_from_pattern(m_exclude, NULL) : nullptr)
{
}

void Rpl::flush()
{
    m_handler->flush_tables();
}

bool Rpl::table_matches(const std::string& ident)
{
    bool rval = false;

    if (!m_match || pcre2_match(m_match,
                                (PCRE2_SPTR)ident.c_str(),
                                PCRE2_ZERO_TERMINATED,
                                0,
                                0,
                                m_md_match,
                                NULL) > 0)
    {
        if (!m_exclude || pcre2_match(m_exclude,
                                      (PCRE2_SPTR)ident.c_str(),
                                      PCRE2_ZERO_TERMINATED,
                                      0,
                                      0,
                                      m_md_exclude,
                                      NULL) == PCRE2_ERROR_NOMATCH)
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Extract the values from a single row  in a row event
 *
 * @param map Table map event associated with this row
 * @param create Table creation associated with this row
 * @param record Avro record used for storing this row
 * @param ptr Pointer to the start of the row data, should be after the row event header
 * @param columns_present The bitfield holding the columns that are present for
 * this row event. Currently this should be a bitfield which has all bits set.
 * @return Pointer to the first byte after the current row event
 */
uint8_t* Rpl::process_row_event_data(STable create, uint8_t* ptr, uint8_t* columns_present, uint8_t* end)
{
    SRowEventHandler& conv = m_handler;
    int npresent = 0;
    long ncolumns = create->columns.size();
    uint8_t* metadata = &create->column_metadata[0];
    size_t metadata_offset = 0;

    /** BIT type values use the extra bits in the row event header */
    int extra_bits = (((ncolumns + 7) / 8) * 8) - ncolumns;
    mxb_assert(ptr < end);

    /** Store the null value bitmap */
    uint8_t* null_bitmap = ptr;
    ptr += (ncolumns + 7) / 8;
    mxb_assert(ptr < end || (bit_is_set(null_bitmap, ncolumns, 0)));

    char trace[ncolumns][768];
    memset(trace, 0, sizeof(trace));

    for (long i = 0; i < ncolumns && npresent < ncolumns; i++)
    {
        if (bit_is_set(columns_present, ncolumns, i))
        {
            npresent++;

            if (bit_is_set(null_bitmap, ncolumns, i))
            {
                sprintf(trace[i], "[%ld] NULL", i);
                conv->column_null(i);
            }
            else if (column_is_fixed_string(create->column_types[i]))
            {
                /** ENUM and SET are stored as STRING types with the type stored
                 * in the metadata. */
                if (fixed_string_is_enum(metadata[metadata_offset]))
                {
                    uint8_t val[metadata[metadata_offset + 1]];
                    uint64_t bytes = unpack_enum(ptr, &metadata[metadata_offset], val);
                    char strval[bytes * 2 + 1];
                    gw_bin2hex(strval, val, bytes);
                    conv->column_string(i, strval);
                    sprintf(trace[i], "[%ld] ENUM: %lu bytes", i, bytes);
                    ptr += bytes;
                    check_overflow(ptr <= end);
                }
                else
                {
                    /**
                     * The first byte in the metadata stores the real type of
                     * the string (ENUM and SET types are also stored as fixed
                     * length strings).
                     *
                     * The first two bits of the second byte contain the XOR'ed
                     * field length but as that information is not relevant for
                     * us, we just use this information to know whether to read
                     * one or two bytes for string length.
                     */

                    uint16_t meta = metadata[metadata_offset + 1] + (metadata[metadata_offset] << 8);
                    int bytes = 0;
                    uint16_t extra_length = (((meta >> 4) & 0x300) ^ 0x300);
                    uint16_t field_length = (meta & 0xff) + extra_length;

                    if (field_length > 255)
                    {
                        bytes = ptr[0] + (ptr[1] << 8);
                        ptr += 2;
                    }
                    else
                    {
                        bytes = *ptr++;
                    }

                    sprintf(trace[i], "[%ld] CHAR: field: %d bytes, data: %d bytes", i, field_length, bytes);
                    char str[bytes + 1];
                    memcpy(str, ptr, bytes);
                    str[bytes] = '\0';
                    conv->column_string(i, str);
                    ptr += bytes;
                    check_overflow(ptr <= end);
                }
            }
            else if (column_is_bit(create->column_types[i]))
            {
                uint8_t len = metadata[metadata_offset + 1];
                uint8_t bit_len = metadata[metadata_offset] > 0 ? 1 : 0;
                size_t bytes = len + bit_len;

                // TODO: extract the bytes
                if (!warn_bit)
                {
                    warn_bit = true;
                    MXS_WARNING("BIT is not currently supported, values are stored as 0.");
                }
                conv->column_int(i, 0);
                sprintf(trace[i], "[%ld] BIT", i);
                ptr += bytes;
                check_overflow(ptr <= end);
            }
            else if (column_is_decimal(create->column_types[i]))
            {
                double f_value = 0.0;
                ptr += unpack_decimal_field(ptr, metadata + metadata_offset, &f_value);
                conv->column_double(i, f_value);
                sprintf(trace[i], "[%ld] DECIMAL", i);
                check_overflow(ptr <= end);
            }
            else if (column_is_variable_string(create->column_types[i]))
            {
                size_t sz;
                int bytes = metadata[metadata_offset] | metadata[metadata_offset + 1] << 8;
                if (bytes > 255)
                {
                    sz = gw_mysql_get_byte2(ptr);
                    ptr += 2;
                }
                else
                {
                    sz = *ptr;
                    ptr++;
                }

                sprintf(trace[i], "[%ld] VARCHAR: field: %d bytes, data: %lu bytes", i, bytes, sz);
                char buf[sz + 1];
                memcpy(buf, ptr, sz);
                buf[sz] = '\0';
                ptr += sz;
                conv->column_string(i, buf);
                check_overflow(ptr <= end);
            }
            else if (column_is_blob(create->column_types[i]))
            {
                uint8_t bytes = metadata[metadata_offset];
                uint64_t len = 0;
                memcpy(&len, ptr, bytes);
                ptr += bytes;
                sprintf(trace[i], "[%ld] BLOB: field: %d bytes, data: %lu bytes", i, bytes, len);
                if (len)
                {
                    conv->column_bytes(i, ptr, len);
                    ptr += len;
                }
                else
                {
                    uint8_t nullvalue = 0;
                    conv->column_bytes(i, &nullvalue, 1);
                }
                check_overflow(ptr <= end);
            }
            else if (column_is_temporal(create->column_types[i]))
            {
                char buf[80];
                ptr += unpack_temporal_value(create->column_types[i], ptr,
                                             &metadata[metadata_offset],
                                             create->columns[i].length,
                                             buf, sizeof(buf));
                conv->column_string(i, buf);
                sprintf(trace[i], "[%ld] %s: %s", i, column_type_to_string(create->column_types[i]), buf);
                check_overflow(ptr <= end);
            }
            /** All numeric types (INT, LONG, FLOAT etc.) */
            else
            {
                uint8_t lval[16];
                memset(lval, 0, sizeof(lval));
                ptr += unpack_numeric_field(ptr,
                                            create->column_types[i],
                                            &metadata[metadata_offset],
                                            lval);
                set_numeric_field_value(conv, i, create->column_types[i], &metadata[metadata_offset], lval,
                                        create->columns[i].is_unsigned);
                sprintf(trace[i], "[%ld] %s", i, column_type_to_string(create->column_types[i]));
                check_overflow(ptr <= end);
            }
            mxb_assert(metadata_offset <= create->column_metadata.size());
            metadata_offset += get_metadata_len(create->column_types[i]);
        }
        else
        {
            sprintf(trace[i], "[%ld] %s: Not present", i, column_type_to_string(create->column_types[i]));
        }

        MXS_INFO("%s", trace[i]);
    }

    return ptr;
}

/**
 * @brief Handle a table map event
 *
 * This converts a table map events into table meta data that will be used when
 * converting binlogs to Avro format.
 * @param router Avro router instance
 * @param hdr Replication header
 * @param ptr Pointer to event payload
 */
bool Rpl::handle_table_map_event(REP_HEADER* hdr, uint8_t* ptr)
{
    bool rval = false;
    uint64_t id;
    char table_ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2];
    int ev_len = m_event_type_hdr_lens[hdr->event_type];

    read_table_info(ptr, ev_len, &id, table_ident, sizeof(table_ident));

    if (!table_matches(table_ident))
    {
        return true;
    }

    auto create = m_created_tables.find(table_ident);

    if (create != m_created_tables.end())
    {
        mxb_assert(create->second->columns.size() > 0);
        auto id = create->second->map_table(ptr, ev_len);
        m_active_maps[id] = create->second;
        MXS_DEBUG("Table %s mapped to %lu", create->second->id().c_str(), id);

        if (!create->second->is_open)
        {
            create->second->is_open = m_handler->open_table(create->second);
        }
    }
    else
    {
        MXS_WARNING("Table map event for table '%s' read before the DDL statement "
                    "for that table  was read. Data will not be processed for this "
                    "table until a DDL statement for it is read.",
                    table_ident);
    }

    return rval;
}

/**
 * @brief Handle a single RBR row event
 *
 * These events contain the changes in the data. This function assumes that full
 * row image is sent in every row event.
 *
 * @param router Avro router instance
 * @param hdr Replication header
 * @param ptr Pointer to the start of the event
 * @return True on succcess, false on error
 */
bool Rpl::handle_row_event(REP_HEADER* hdr, uint8_t* ptr)
{
    bool rval = false;
    uint8_t* end = ptr + hdr->event_size - BINLOG_EVENT_HDR_LEN;
    uint8_t table_id_size = m_event_type_hdr_lens[hdr->event_type] == 6 ? 4 : 6;
    uint64_t table_id = 0;

    /** The first value is the ID where the table was mapped. This should be
     * the same as the ID in the table map even which was processed before this
     * row event. */
    memcpy(&table_id, ptr, table_id_size);
    ptr += table_id_size;

    /** Replication flags, currently ignored for the most part. */
    uint16_t flags = 0;
    memcpy(&flags, ptr, 2);
    ptr += 2;

    if (table_id == TABLE_DUMMY_ID && flags & ROW_EVENT_END_STATEMENT)
    {
        /** This is an dummy event which should release all table maps. Right
         * now we just return without processing the rows. */
        return true;
    }

    /** Newer replication events have extra data stored in the header. MariaDB
     * 10.1 does not use these and instead use the v1 events */
    if (hdr->event_type > DELETE_ROWS_EVENTv1)
    {
        /** Version 2 row event, skip extra data */
        uint16_t extra_len = 0;
        memcpy(&extra_len, ptr, 2);
        ptr += 2 + extra_len;
    }

    /** Number of columns in the table */
    uint64_t ncolumns = mxq::leint_consume(&ptr);

    /** If full row image is used, all columns are present. Currently only full
     * row image is supported and thus the bitfield should be all ones. In
     * the future partial row images could be used if the bitfield containing
     * the columns that are present in this event is used. */
    const int coldata_size = (ncolumns + 7) / 8;
    uint8_t col_present[coldata_size];
    memcpy(&col_present, ptr, coldata_size);
    ptr += coldata_size;

    /** Update events have the before and after images of the row. This can be
     * used to calculate a "delta" of sorts if necessary. Currently we store
     * both the before and the after images. */
    uint8_t col_update[coldata_size];
    if (hdr->event_type == UPDATE_ROWS_EVENTv1
        || hdr->event_type == UPDATE_ROWS_EVENTv2)
    {
        memcpy(&col_update, ptr, coldata_size);
        ptr += coldata_size;
    }

    // There should always be a table map event prior to a row event.

    auto it = m_active_maps.find(table_id);

    if (it != m_active_maps.end())
    {
        auto table_ident = it->second->id();

        if (!table_matches(table_ident))
        {
            return true;
        }

        auto create = it->second;

        if (ncolumns != create->columns.size())
        {
            MXS_ERROR("Row event and table map event have different column "
                      "counts for table %s, only full row image is currently "
                      "supported.", create->id().c_str());
        }
        else if (m_handler->prepare_table(create))
        {
            /** Each event has one or more rows in it. The number of rows is not known
             * beforehand so we must continue processing them until we reach the end
             * of the event. */
            int rows = 0;
            MXS_INFO("Row Event for '%s' at %u", table_ident.c_str(), hdr->next_pos - hdr->event_size);

            while (ptr < end)
            {
                int event_type = get_event_type(hdr->event_type);

                // Increment the event count for this transaction
                m_gtid.event_num++;

                m_handler->prepare_row(m_gtid, *hdr, event_type);
                ptr = process_row_event_data(create, ptr, col_present, end);
                m_handler->commit(m_gtid);

                /** Update rows events have the before and after images of the
                 * affected rows so we'll process them as another record with
                 * a different type */
                if (event_type == UPDATE_EVENT)
                {
                    m_gtid.event_num++;
                    m_handler->prepare_row(m_gtid, *hdr, UPDATE_EVENT_AFTER);
                    ptr = process_row_event_data(create, ptr, col_present, end);
                    m_handler->commit(m_gtid);
                }

                rows++;
            }

            rval = true;
        }
        else
        {
            MXS_ERROR("Avro file handle was not found for table %s. See earlier"
                      " errors for more details.", create->id().c_str());
        }
    }
    else
    {
        MXS_INFO("Row event for unknown table mapped to ID %lu. Data will not "
                 "be processed.", table_id);
    }

    return rval;
}

/**
 * Save the CREATE TABLE statement to disk and replace older versions of the table
 * in the router's hashtable.
 * @param router Avro router instance
 * @param created Created table
 * @return False if an error occurred and true if successful
 */
bool Rpl::save_and_replace_table_create(STable created)
{
    std::string table_ident = created->id();
    created->version = ++m_versions[table_ident];
    created->is_open = false;
    m_created_tables[table_ident] = created;
    mxb_assert(created->columns.size() > 0);
    return m_handler->create_table(created);
}

bool Rpl::rename_table_create(STable created, const std::string& old_id)
{
    m_created_tables.erase(old_id);
    return save_and_replace_table_create(created);
}

/**
 * @brief Handling of query events
 *
 * @param router Avro router instance
 * @param hdr Replication header
 * @param pending_transaction Pointer where status of pending transaction is stored
 * @param ptr Pointer to the start of the event payload
 */
void Rpl::handle_query_event(REP_HEADER* hdr, uint8_t* ptr)
{
    constexpr int DBNM_OFF = 8;                 // Database name offset
    constexpr int VBLK_OFF = 4 + 4 + 1 + 2;     // Varblock offset
    constexpr int PHDR_OFF = 4 + 4 + 1 + 2 + 2; // Post-header offset

    int dblen = ptr[DBNM_OFF];
    int vblklen = gw_mysql_get_byte2(ptr + VBLK_OFF);
    int len = hdr->event_size - BINLOG_EVENT_HDR_LEN - (PHDR_OFF + vblklen + 1 + dblen);
    std::string sql((char*) ptr + PHDR_OFF + vblklen + 1 + dblen, len);
    std::string db((char*) ptr + PHDR_OFF + vblklen, dblen);

    normalize_sql_string(sql);

    static bool warn_not_row_format = true;

    if (warn_not_row_format)
    {
        GWBUF* buffer = gwbuf_alloc(sql.length() + 5);
        gw_mysql_set_byte3(GWBUF_DATA(buffer), sql.length() + 1);
        GWBUF_DATA(buffer)[4] = 0x03;
        memcpy(GWBUF_DATA(buffer) + 5, sql.c_str(), sql.length());
        qc_query_op_t op = qc_get_operation(buffer);
        gwbuf_free(buffer);

        if (op == QUERY_OP_UPDATE || op == QUERY_OP_INSERT || op == QUERY_OP_DELETE)
        {
            MXS_WARNING("Possible STATEMENT or MIXED format binary log. Check that "
                        "'binlog_format' is set to ROW on the master.");
            warn_not_row_format = false;
        }
    }

    parse_sql(sql, db);
}

void Rpl::handle_event(REP_HEADER hdr, uint8_t* ptr)
{
    if (m_binlog_checksum)
    {
        // We don't care about the checksum at this point so we ignore it
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
        m_event_type_hdr_lens.assign(ptr, ptr + n_events);
        m_event_types = n_events;
        m_binlog_checksum = checksum[0];
    }
    else if (hdr.event_type == TABLE_MAP_EVENT)
    {
        handle_table_map_event(&hdr, ptr);
    }
    else if ((hdr.event_type >= WRITE_ROWS_EVENTv0 && hdr.event_type <= DELETE_ROWS_EVENTv1)
             || (hdr.event_type >= WRITE_ROWS_EVENTv2 && hdr.event_type <= DELETE_ROWS_EVENTv2))
    {
        handle_row_event(&hdr, ptr);
    }
    else if (hdr.event_type == GTID_EVENT)
    {
        m_gtid.extract(hdr, ptr);
    }
    else if (hdr.event_type == QUERY_EVENT)
    {
        handle_query_event(&hdr, ptr);
    }
}

// Sanitizes the SQL field names for Avro usage
static std::string avro_sanitizer(const char* s, int l)
{
    std::string str(s, l);

    for (auto& a : str)
    {
        if (!isalnum(a) && a != '_')
        {
            a = '_';
        }
    }

    if (is_reserved_word(str.c_str()))
    {
        str += '_';
    }

    return str;
}

void Rpl::parse_sql(const std::string& sql, const std::string& db)
{
    MXS_INFO("%s", sql.c_str());
    parser.db = db;
    parser.tokens = tok::Tokenizer::tokenize(sql.c_str(), avro_sanitizer);

    try
    {
        switch (chomp().type())
        {
        case tok::REPLACE:
        case tok::CREATE:
            discard({tok::OR, tok::REPLACE});
            assume(tok::TABLE);
            discard({tok::IF, tok::NOT, tok::EXISTS});
            create_table();
            break;

        case tok::ALTER:
            discard({tok::ONLINE, tok::IGNORE});
            assume(tok::TABLE);
            alter_table();
            break;

        case tok::DROP:
            assume(tok::TABLE);
            discard({tok::IF, tok::EXISTS});
            drop_table();
            break;

        case tok::RENAME:
            assume(tok::TABLE);
            rename_table();
            break;

        default:
            break;
        }
    }
    catch (const ParsingError& err)
    {
        MXS_INFO("Parsing failed: %s (%s)", err.what(), sql.c_str());
    }
}

tok::Type Rpl::next()
{
    return parser.tokens.front().type();
}

tok::Tokenizer::Token Rpl::chomp()
{
    return parser.tokens.chomp();
}

tok::Tokenizer::Token Rpl::assume(tok::Type t)
{
    if (next() != t)
    {
        throw ParsingError("Expected " + tok::Tokenizer::Token::to_string(t)
                           + ", got " + parser.tokens.front().to_string());
    }

    return chomp();
}

bool Rpl::expect(const std::vector<tok::Type>& types)
{
    bool rval = true;
    auto it = parser.tokens.begin();

    for (auto t : types)
    {
        if (it == parser.tokens.end() || it->type() != t)
        {
            rval = false;
            break;
        }

        ++it;
    }

    return rval;
}

void Rpl::discard(const std::unordered_set<tok::Type>& types)
{
    while (types.count(next()))
    {
        chomp();
    }
}

void Rpl::parentheses()
{
    if (next() == tok::LP)
    {
        chomp();
        int depth = 1;

        while (next() != tok::EXHAUSTED && depth > 0)
        {
            switch (chomp().type())
            {
            case tok::LP:
                depth++;
                break;

            case tok::RP:
                depth--;
                break;

            default:
                break;
            }
        }

        if (depth > 0)
        {
            throw ParsingError("Could not find closing parenthesis");
        }
    }
}

void Rpl::table_identifier()
{
    if (expect({tok::ID, tok::DOT, tok::ID}))
    {
        parser.db = chomp().value();
        chomp();
        parser.table = chomp().value();
    }
    else if (expect({tok::ID}))
    {
        parser.table = chomp().value();
    }
    else
    {
        throw ParsingError("Syntax error, have " + parser.tokens.front().to_string()
                           + " expected identifier");
    }
}

Column Rpl::column_def()
{
    Column c(assume(tok::ID).value());
    parentheses();      // Field length, if defined
    c.is_unsigned = next() == tok::UNSIGNED;

    // Ignore the rest of the field definition, we aren't interested in it
    while (next() != tok::EXHAUSTED)
    {
        parentheses();

        switch (chomp().type())
        {
        case tok::COMMA:
            return c;

        case tok::AFTER:
            c.after = assume(tok::ID).value();
            break;

        case tok::FIRST:
            c.first = true;
            break;

        default:
            break;
        }
    }

    return c;
}

void Rpl::create_table()
{
    table_identifier();

    if (expect({tok::LIKE}) || expect({tok::LP, tok::LIKE}))
    {
        // CREATE TABLE ... LIKE ...
        if (chomp().type() == tok::LP)
        {
            chomp();
        }

        auto new_db = parser.db;
        auto new_table = parser.table;
        table_identifier();
        auto old_db = parser.db;
        auto old_table = parser.table;

        do_create_table_like(old_db, old_table, new_db, new_table);
    }
    else
    {
        // CREATE TABLE ...
        assume(tok::LP);
        do_create_table();
    }
}

void Rpl::drop_table()
{
    table_identifier();
    m_created_tables.erase(parser.db + '.' + parser.table);
}

void Rpl::alter_table()
{
    table_identifier();

    auto it = m_created_tables.find(parser.db + '.' + parser.table);

    if (it == m_created_tables.end())
    {
        throw ParsingError("Table not found: " + parser.db + '.' + parser.table);
    }

    auto create = it->second;
    bool updated = false;

    while (next() != tok::EXHAUSTED)
    {
        switch (chomp().type())
        {
        case tok::ADD:
            discard({tok::COLUMN, tok::IF, tok::NOT, tok::EXISTS});

            if (next() == tok::ID || next() == tok::LP)
            {
                alter_table_add_column(create);
                updated = true;
            }
            break;

        case tok::DROP:
            discard({tok::COLUMN, tok::IF, tok::EXISTS});

            if (next() == tok::ID)
            {
                alter_table_drop_column(create);
                updated = true;
            }
            break;

        case tok::MODIFY:
            discard({tok::COLUMN, tok::IF, tok::EXISTS});

            if (next() == tok::ID)
            {
                alter_table_modify_column(create);
                updated = true;
            }
            break;

        case tok::CHANGE:
            discard({tok::COLUMN, tok::IF, tok::EXISTS});

            if (next() == tok::ID)
            {
                alter_table_change_column(create);
                updated = true;
            }
            break;

        case tok::RENAME:
            {
                auto old_db = parser.db;
                auto old_table = parser.table;
                discard({tok::TO});

                table_identifier();
                auto new_db = parser.db;
                auto new_table = parser.table;
                discard({tok::COMMA});

                do_table_rename(old_db, old_table, old_db, new_table);
            }
            break;

        default:
            break;
        }
    }

    if (updated && create->is_open)
    {
        // The ALTER statement can modify multiple parts of the table which is why we synchronize the table
        // only once we've fully processed the statement. In addition to this, the table is only synced if at
        // least one row event for it has been created.
        create->version = ++m_versions[create->database + '.' + create->table];
        create->is_open = false;
        m_handler->create_table(create);
    }
}

void Rpl::alter_table_add_column(const STable& create)
{
    if (next() == tok::LP)
    {
        // ALTER TABLE ... ADD (column definition, ...)
        chomp();

        while (next() != tok::EXHAUSTED)
        {
            create->columns.push_back(column_def());
        }
    }
    else
    {
        // ALTER TABLE ... ADD column definition [FIRST | AFTER ...]
        do_add_column(create, column_def());
    }
}

void Rpl::alter_table_drop_column(const STable& create)
{
    do_drop_column(create, chomp().value());
    discard({tok::RESTRICT, tok::CASCADE});
}

void Rpl::alter_table_modify_column(const STable& create)
{
    do_change_column(create, parser.tokens.front().value());
}

void Rpl::alter_table_change_column(const STable& create)
{
    do_change_column(create, chomp().value());
}

void Rpl::rename_table()
{
    do
    {
        table_identifier();
        auto old_db = parser.db;
        auto old_table = parser.table;

        assume(tok::TO);

        table_identifier();
        auto new_db = parser.db;
        auto new_table = parser.table;

        do_table_rename(old_db, old_table, new_db, new_table);

        discard({tok::COMMA});
    }
    while (next() != tok::EXHAUSTED);
}

void Rpl::do_create_table()
{
    std::vector<Column> columns;

    do
    {
        columns.push_back(column_def());
    }
    while (next() == tok::ID);

    STable tbl(new Table(parser.db, parser.table, 0, std::move(columns)));
    save_and_replace_table_create(tbl);
}

void Rpl::do_create_table_like(const std::string& old_db, const std::string& old_table,
                               const std::string& new_db, const std::string& new_table)
{
    auto it = m_created_tables.find(old_db + '.' + old_table);

    if (it != m_created_tables.end())
    {
        auto cols = it->second->columns;
        STable tbl(new Table(new_db, new_table, 1, std::move(cols)));
        save_and_replace_table_create(tbl);
    }
    else
    {
        MXS_ERROR("Could not find source table %s.%s", parser.db.c_str(), parser.table.c_str());
    }
}

void Rpl::do_table_rename(const std::string& old_db, const std::string& old_table,
                          const std::string& new_db, const std::string& new_table)
{
    auto from = old_db + '.' + old_table;
    auto to = new_db + '.' + new_table;
    auto it = m_created_tables.find(from);

    if (it != m_created_tables.end())
    {
        it->second->database = new_db;
        it->second->table = new_table;
        rename_table_create(it->second, from);
    }
}

void Rpl::do_add_column(const STable& create, Column c)
{
    auto& cols = create->columns;

    if (c.first)
    {
        cols.insert(cols.begin(), c);
    }
    else if (!c.after.empty())
    {
        auto it = std::find_if(cols.begin(), cols.end(), [&](const auto& a) {
                                   return a.name == c.after;
                               });

        if (it == cols.end())
        {
            throw ParsingError("Could not find field '" + c.after
                               + "' for ALTER TABLE ADD COLUMN ... AFTER");
        }

        cols.insert(++it, c);
    }
    else
    {
        cols.push_back(c);
    }
}

void Rpl::do_drop_column(const STable& create, const std::string& name)
{
    auto& cols = create->columns;

    auto it = std::find_if(cols.begin(), cols.end(), [&name](const auto& f) {
                               return f.name == name;
                           });
    if (it == cols.end())
    {
        throw ParsingError("Could not find field '" + name
                           + "' for table " + parser.db + '.' + parser.table);
    }

    cols.erase(it);
}

void Rpl::do_change_column(const STable& create, const std::string& old_name)
{
    Column c = column_def();

    if (c.first || !c.after.empty())
    {
        do_drop_column(create, old_name);
        do_add_column(create, c);
    }
    else
    {
        auto& cols = create->columns;
        auto it = std::find_if(cols.begin(), cols.end(), [&](const auto& a) {
                                   return a.name == old_name;
                               });

        if (it != cols.end())
        {
            *it = c;
        }
        else
        {
            throw ParsingError("Could not find column " + old_name);
        }
    }
}

void Rpl::load_metadata(const std::string& datadir)
{
    char path[PATH_MAX + 1];
    snprintf(path, sizeof(path), "%s/*.avsc", datadir.c_str());
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
            char* dbstart = strrchr(files.gl_pathv[i], '/');

            if (!dbstart)
            {
                continue;
            }

            dbstart++;

            char* tablestart = strchr(dbstart, '.');

            if (!tablestart)
            {
                continue;
            }

            snprintf(db, sizeof(db), "%.*s", (int)(tablestart - dbstart), dbstart);
            tablestart++;

            char* versionstart = strchr(tablestart, '.');

            if (!versionstart)
            {
                continue;
            }

            snprintf(table, sizeof(table), "%.*s", (int)(versionstart - tablestart), tablestart);
            versionstart++;

            char* suffix = strchr(versionstart, '.');
            char* versionend = NULL;
            version = strtol(versionstart, &versionend, 10);

            if (versionend == suffix)
            {
                snprintf(table_ident, sizeof(table_ident), "%s.%s", db, table);

                if (auto create = load_table_from_schema(files.gl_pathv[i], db, table, version))
                {
                    if (m_versions[create->id()] < create->version)
                    {
                        m_versions[create->id()] = create->version;
                        m_created_tables[create->id()] = create;
                    }
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
