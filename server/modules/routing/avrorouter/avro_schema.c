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
 * @file avro_schema.c - Avro schema related functions
 */

#include "avrorouter.h"

#include <maxscale/mysql_utils.h>
#include <jansson.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <maxscale/log_manager.h>
#include <sys/stat.h>
#include <errno.h>
#include <maxscale/debug.h>
#include <string.h>
#include <strings.h>
#include <maxscale/alloc.h>

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
char* json_new_schema_from_table(TABLE_MAP *map)
{
    TABLE_CREATE *create = map->table_create;

    if (map->version != create->version)
    {
        MXS_ERROR("Version mismatch for table %s.%s. Table map version is %d and "
                  "the table definition version is %d.", map->database, map->table,
                  map->version, create->version);
        return NULL;
    }

    json_error_t err;
    memset(&err, 0, sizeof(err));
    json_t *schema = json_object();
    json_object_set_new(schema, "namespace", json_string("MaxScaleChangeDataSchema.avro"));
    json_object_set_new(schema, "type", json_string("record"));
    json_object_set_new(schema, "name", json_string("ChangeRecord"));

    json_t *array = json_array();
    json_array_append_new(array, json_pack_ex(&err, 0, "{s:s, s:s}", "name",
                                              avro_domain, "type", "int"));
    json_array_append_new(array, json_pack_ex(&err, 0, "{s:s, s:s}", "name",
                                              avro_server_id, "type", "int"));
    json_array_append_new(array, json_pack_ex(&err, 0, "{s:s, s:s}", "name",
                                              avro_sequence, "type", "int"));
    json_array_append_new(array, json_pack_ex(&err, 0, "{s:s, s:s}", "name",
                                              avro_event_number, "type", "int"));
    json_array_append_new(array, json_pack_ex(&err, 0, "{s:s, s:s}", "name",
                                              avro_timestamp, "type", "int"));

    /** Enums and other complex types are defined with complete JSON objects
     * instead of string values */
    json_t *event_types = json_pack_ex(&err, 0, "{s:s, s:s, s:[s,s,s,s]}", "type", "enum",
                                       "name", "EVENT_TYPES", "symbols", "insert",
                                       "update_before", "update_after", "delete");

    // Ownership of `event_types` is stolen when using the `o` format
    json_array_append_new(array, json_pack_ex(&err, 0, "{s:s, s:o}", "name", avro_event_type,
                                              "type", event_types));

    for (uint64_t i = 0; i < map->columns && i < create->columns; i++)
    {
        ss_info_dassert(create->column_names[i] && *create->column_names[i],
                        "Column name should not be empty or NULL");
        ss_info_dassert(create->column_types[i] && *create->column_types[i],
                        "Column type should not be empty or NULL");

        json_array_append_new(array, json_pack_ex(&err, 0, "{s:s, s:s, s:s, s:i}",
                                                  "name", create->column_names[i],
                                                  "type", column_type_to_avro_type(map->column_types[i]),
                                                  "real_type", create->column_types[i],
                                                  "length", create->column_lengths[i]));
    }
    json_object_set_new(schema, "fields", array);
    char* rval = json_dumps(schema, JSON_PRESERVE_ORDER);
    json_decref(schema);
    return rval;
}

/**
 * @brief Check whether the field is one that was generated by the avrorouter
 *
 * @param name Name of the field in the Avro schema
 * @return True if field was not generated by the avrorouter
 */
static inline bool not_generated_field(const char* name)
{
    return strcmp(name, avro_domain) && strcmp(name, avro_server_id) &&
           strcmp(name, avro_sequence) && strcmp(name, avro_event_number) &&
           strcmp(name, avro_event_type) && strcmp(name, avro_timestamp);
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
bool json_extract_field_names(const char* filename, TABLE_CREATE *table)
{
    bool rval = false;
    json_error_t err;
    err.text[0] = '\0';
    json_t *obj, *arr;

    if ((obj = json_load_file(filename, 0, &err)) && (arr = json_object_get(obj, "fields")))
    {
        ss_dassert(json_is_array(arr));
        if (json_is_array(arr))
        {
            int array_size = json_array_size(arr);
            table->column_names = (char**)MXS_MALLOC(sizeof(char*) * (array_size));
            table->column_types = (char**)MXS_MALLOC(sizeof(char*) * (array_size));
            table->column_lengths = (int*)MXS_MALLOC(sizeof(int) * (array_size));

            if (table->column_names && table->column_types && table->column_lengths)
            {
                int columns = 0;
                rval = true;

                for (int i = 0; i < array_size; i++)
                {
                    json_t* val = json_array_get(arr, i);

                    if (json_is_object(val))
                    {
                        json_t* value;

                        if ((value = json_object_get(val, "real_type")) && json_is_string(value))
                        {
                            table->column_types[columns] = MXS_STRDUP_A(json_string_value(value));
                        }
                        else
                        {
                            table->column_types[columns] = MXS_STRDUP_A("unknown");
                            MXS_WARNING("No \"real_type\" value defined. Treating as unknown type field.");
                        }

                        if ((value = json_object_get(val, "length")) && json_is_integer(value))
                        {
                            table->column_lengths[columns] = json_integer_value(value);
                        }
                        else
                        {
                            table->column_lengths[columns] = -1;
                            MXS_WARNING("No \"length\" value defined. Treating as default length field.");
                        }

                        json_t *name = json_object_get(val, "name");

                        if (name && json_is_string(name))
                        {
                            const char *name_str = json_string_value(name);

                            if (not_generated_field(name_str))
                            {
                                table->column_names[columns] = MXS_STRDUP_A(name_str);

                                json_t* value;

                                if ((value = json_object_get(val, "real_type")) && json_is_string(value))
                                {
                                    table->column_types[columns] = MXS_STRDUP_A(json_string_value(value));
                                }
                                else
                                {
                                    table->column_types[columns] = MXS_STRDUP_A("unknown");
                                    MXS_WARNING("No \"real_type\" value defined. "
                                                "Treating as unknown type field.");
                                }

                                if ((value = json_object_get(val, "length")) && json_is_integer(value))
                                {
                                    table->column_lengths[columns] = json_integer_value(value);
                                }
                                else
                                {
                                    table->column_lengths[columns] = -1;
                                    MXS_WARNING("No \"length\" value defined. "
                                                "Treating as default length field.");
                                }

                                columns++;
                            }
                        }
                        else
                        {
                            MXS_ERROR("JSON value for \"name\" was not a string in "
                                      "file '%s'.", filename);
                            rval = false;
                        }
                    }
                    else
                    {
                        MXS_ERROR("JSON value for \"fields\" was not an array of objects in "
                                  "file '%s'.", filename);
                        rval = false;
                    }
                }
                table->columns = columns;
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
        MXS_ERROR("Failed to load JSON from file '%s': %s", filename,
                  obj && !arr ? "No 'fields' value in object." : err.text);
    }

    return rval;
}

/**
 * @brief Save the Avro schema of a table to disk
 *
 * @param path Schema directory
 * @param schema Schema in JSON format
 * @param map Table map that @p schema represents
 */
void save_avro_schema(const char *path, const char* schema, TABLE_MAP *map)
{
    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.%s.%06d.avsc", path, map->database,
             map->table, map->version);

    if (access(filepath, F_OK) != 0)
    {
        if (!map->table_create->was_used)
        {
            FILE *file = fopen(filepath, "wb");
            if (file)
            {
                fprintf(file, "%s\n", schema);
                map->table_create->was_used = true;
                fclose(file);
            }
        }
    }
    else
    {
        MXS_NOTICE("Schema version %d already exists: %s", map->version, filepath);
    }
}

/**
 * Extract the table definition from a CREATE TABLE statement
 * @param sql The SQL statement
 * @param size Length of the statement
 * @return Pointer to the start of the definition of NULL if the query is
 * malformed.
 */
static const char* get_table_definition(const char *sql, int len, int* size)
{
    const char *rval = NULL;
    const char *ptr = sql;
    const char *end = sql + len;
    while (ptr < end && *ptr != '(')
    {
        ptr++;
    }

    /** We assume at least the parentheses are in the statement */
    if (ptr < end - 2)
    {
        int depth = 0;
        ptr++;
        const char *start = ptr; // Skip first parenthesis
        while (ptr < end)
        {
            switch (*ptr)
            {
            case '(':
                depth++;
                break;

            case ')':
                depth--;
                break;

            default:
                break;
            }

            /** We found the last closing parenthesis */
            if (depth < 0)
            {
                *size = ptr - start;
                rval = start;
                break;
            }
            ptr++;
        }
    }

    return rval;
}

/**
 * Extract the table name from a CREATE TABLE statement
 * @param sql SQL statement
 * @param dest Destination where the table name is extracted. Must be at least
 * MYSQL_TABLE_MAXLEN bytes long.
 * @return True if extraction was successful
 */
static bool get_table_name(const char* sql, char* dest)
{
    bool rval = false;
    const char* ptr = strchr(sql, '(');

    if (ptr)
    {
        ptr--;
        while (*ptr == '`' || isspace(*ptr))
        {
            ptr--;
        }

        const char* end = ptr + 1;
        while (*ptr != '`' && *ptr != '.' && !isspace(*ptr))
        {
            ptr--;
        }
        ptr++;
        memcpy(dest, ptr, end - ptr);
        dest[end - ptr] = '\0';
        rval = true;
    }

    return rval;
}

/**
 * Extract the database name from a CREATE TABLE statement
 *
 * @param sql SQL statement
 * @param dest Destination where the database name is extracted. Must be at least
 *             MYSQL_DATABASE_MAXLEN bytes long.
 *
 * @return True if a database name was extracted
 */
static bool get_database_name(const char* sql, char* dest)
{
    bool rval = false;
    const char* ptr = strchr(sql, '(');

    if (ptr)
    {
        ptr--;
        while (*ptr == '`' || isspace(*ptr))
        {
            ptr--;
        }

        while (*ptr != '`' && *ptr != '.' && !isspace(*ptr))
        {
            ptr--;
        }

        if (*ptr == '.')
        {
            // The query defines an explicit database

            while (*ptr == '`' || *ptr == '.' || isspace(*ptr))
            {
                ptr--;
            }

            const char* end = ptr + 1;

            while (*ptr != '`' && *ptr != '.' && !isspace(*ptr))
            {
                ptr--;
            }

            ptr++;
            memcpy(dest, ptr, end - ptr);
            dest[end - ptr] = '\0';
            rval = true;
        }
    }

    return rval;
}

void make_valid_avro_identifier(char* ptr)
{
    while (*ptr)
    {
        if (!isalnum(*ptr) && *ptr != '_')
        {
            *ptr = '_';
        }
        ptr++;
    }
}

const char* next_field_definition(const char* ptr)
{
    int depth = 0;
    bool quoted = false;
    char qchar;

    while (*ptr)
    {
        if (!quoted)
        {
            if (*ptr == '(')
            {
                depth++;
            }
            else if (*ptr == ')')
            {
                depth--;
            }
            else if (*ptr == '"' || *ptr == '\'')
            {
                qchar = *ptr;
                quoted = true;
            }
            else if (*ptr == ',' && depth == 0 && !quoted)
            {
                ptr++;
                break;
            }
        }
        else if (qchar == *ptr)
        {
            quoted = false;
        }

        ptr++;
    }

    return ptr;
}

static const char *extract_field_name(const char* ptr, char* dest, size_t size)
{
    bool bt = false;

    while (*ptr && (isspace(*ptr) || (bt = *ptr == '`')))
    {
        ptr++;
        if (bt)
        {
            break;
        }
    }

    if (!bt)
    {
        if (strncasecmp(ptr, "constraint", 10) == 0 || strncasecmp(ptr, "index", 5) == 0 ||
            strncasecmp(ptr, "key", 3) == 0 || strncasecmp(ptr, "fulltext", 8) == 0 ||
            strncasecmp(ptr, "spatial", 7) == 0 || strncasecmp(ptr, "foreign", 7) == 0 ||
            strncasecmp(ptr, "unique", 6) == 0 || strncasecmp(ptr, "primary", 7) == 0)
        {
            // Found a keyword
            return NULL;
        }
    }

    const char *start = ptr;

    if (!bt)
    {
        while (*ptr && !isspace(*ptr))
        {
            ptr++;
        }
    }
    else
    {
        while (*ptr && *ptr != '`')
        {
            ptr++;
        }
    }

    if (ptr > start)
    {
        /** Valid identifier */
        size_t bytes = ptr - start;

        memcpy(dest, start, bytes);
        dest[bytes] = '\0';

        make_valid_avro_identifier(dest);
        ss_dassert(strlen(dest) > 0);
    }
    else
    {
        ptr = NULL;
    }

    return ptr;
}

int extract_type_length(const char* ptr, char *dest)
{
    /** Skip any leading whitespace */
    while (*ptr && (isspace(*ptr) || *ptr == '`'))
    {
        ptr++;
    }

    /** The field type definition starts here */
    const char *start = ptr;

    /** Skip characters until we either hit a whitespace character or the start
     * of the length definition. */
    while (*ptr && isalpha(*ptr))
    {
        ptr++;
    }

    /** Store type */
    for (const char* c = start; c < ptr; c++)
    {
        *dest++ = tolower(*c);
    }

    *dest++ = '\0';

    /** Skip whitespace */
    while (*ptr && isspace(*ptr))
    {
        ptr++;
    }

    int rval = -1; // No length defined

    /** Start of length definition */
    if (*ptr == '(')
    {
        ptr++;
        char *end;
        int val = strtol(ptr, &end, 10);

        if (*end == ')')
        {
            rval = val;
        }
    }

    return rval;
}

int count_columns(const char* ptr)
{
    int i = 2;

    while ((ptr = strchr(ptr, ',')))
    {
        ptr++;
        i++;
    }

    return i;
}

/**
 * Process a table definition into an array of column names
 * @param nameptr table definition
 * @return Number of processed columns or -1 on error
 */
static int process_column_definition(const char *nameptr, char*** dest, char*** dest_types, int** dest_lens)
{
    int n = count_columns(nameptr);
    *dest = MXS_MALLOC(sizeof(char*) * n);
    *dest_types = MXS_MALLOC(sizeof(char*) * n);
    *dest_lens = MXS_MALLOC(sizeof(int) * n);

    char **names = *dest;
    char **types = *dest_types;
    int *lengths = *dest_lens;
    char colname[512];
    int i = 0;

    while ((nameptr = extract_field_name(nameptr, colname, sizeof(colname))))
    {
        ss_dassert(i < n);
        char type[100] = "";
        int len = extract_type_length(nameptr, type);
        nameptr = next_field_definition(nameptr);
        fix_reserved_word(colname);

        lengths[i] = len;
        types[i] = MXS_STRDUP_A(type);
        names[i] = MXS_STRDUP_A(colname);
        ss_info_dassert(*names[i] && *types[i], "`name` and `type` must not be empty");
        i++;
    }

    return i;
}

TABLE_CREATE* table_create_from_schema(const char* file, const char* db,
                                       const char* table, int version)
{
    db = MXS_STRDUP(db);
    table = MXS_STRDUP(table);

    TABLE_CREATE* newtable = (TABLE_CREATE*)MXS_MALLOC(sizeof(TABLE_CREATE));

    if (!db || !table || !newtable)
    {
        MXS_FREE((void*)db);
        MXS_FREE((void*)table);
        MXS_FREE(newtable);

        return NULL;
    }

    newtable->table = (char*)table;
    newtable->database = (char*)db;
    newtable->version = version;
    newtable->was_used = true;

    if (!json_extract_field_names(file, newtable))
    {
        MXS_FREE(newtable->table);
        MXS_FREE(newtable->database);
        MXS_FREE(newtable);
        newtable = NULL;
    }

    return newtable;
}

/**
 * @brief Handle a query event which contains a CREATE TABLE statement
 * @param sql Query SQL
 * @param db Database where this query was executed
 * @return New CREATE_TABLE object or NULL if an error occurred
 */
TABLE_CREATE* table_create_alloc(const char* sql, int len, const char* db)
{
    /** Extract the table definition so we can get the column names from it */
    int stmt_len = 0;
    const char* statement_sql = get_table_definition(sql, len, &stmt_len);
    ss_dassert(statement_sql);
    char table[MYSQL_TABLE_MAXLEN + 1];
    char database[MYSQL_DATABASE_MAXLEN + 1];
    const char* err = NULL;
    MXS_INFO("Create table: %.*s", len, sql);

    if (!statement_sql)
    {
        err = "table definition";
    }
    else if (!get_table_name(sql, table))
    {
        err = "table name";
    }

    if (get_database_name(sql, database))
    {
        // The CREATE statement contains the database name
        db = database;
    }
    else if (*db == '\0')
    {
        // No explicit or current database
        err = "database name";
    }

    if (err)
    {
        MXS_ERROR("Malformed CREATE TABLE statement, could not extract %s: %.*s", err, len, sql);
        return NULL;
    }

    int* lengths = NULL;
    char **names = NULL;
    char **types = NULL;
    int n_columns = process_column_definition(statement_sql, &names, &types, &lengths);
    ss_dassert(n_columns > 0);

    /** We have appear to have a valid CREATE TABLE statement */
    TABLE_CREATE *rval = NULL;
    if (n_columns > 0)
    {
        if ((rval = MXS_MALLOC(sizeof(TABLE_CREATE))))
        {
            rval->version = 1;
            rval->was_used = false;
            rval->column_names = names;
            rval->column_lengths = lengths;
            rval->column_types = types;
            rval->columns = n_columns;
            rval->database = MXS_STRDUP(db);
            rval->table = MXS_STRDUP(table);
        }

        if (rval == NULL || rval->database == NULL || rval->table == NULL)
        {
            if (rval)
            {
                MXS_FREE(rval->database);
                MXS_FREE(rval->table);
                MXS_FREE(rval);
            }

            for (int i = 0; i < n_columns; i++)
            {
                MXS_FREE(names[i]);
            }

            MXS_FREE(names);
            rval = NULL;
        }
    }
    else
    {
        MXS_ERROR("No columns in a CREATE TABLE statement: %.*s", stmt_len, statement_sql);
    }
    return rval;
}

static const char* TOK_CREATE[] =
{
    "CREATE",
    NULL
};

static const char* TOK_TABLE[] =
{
    "TABLE",
    NULL
};

static const char* TOK_GROUP_REPLACE[] =
{
    "OR",
    "REPLACE",
    NULL
};

static const char* TOK_GROUP_EXISTS[] =
{
    "IF",
    "NOT",
    "EXISTS",
    NULL
};

/**
 * Read one token (i.e. SQL keyword)
 */
static const char* get_token(const char* ptr, const char* end, char* dest)
{
    while (ptr < end && isspace(*ptr))
    {
        ptr++;
    }

    const char* start = ptr;

    while (ptr < end && !isspace(*ptr))
    {
        ptr++;
    }

    size_t len = ptr - start;
    memcpy(dest, start, len);
    dest[len] = '\0';

    return ptr;
}

/**
 * Consume one token
 */
static bool chomp_one_token(const char* expected, const char** ptr, const char* end, char* buf)
{
    bool rval = false;
    const char* next = get_token(*ptr, end, buf);

    if (strcasecmp(buf, expected) == 0)
    {
        rval = true;
        *ptr = next;
    }

    return rval;
}

/**
 * Consume all tokens in a group
 */
static bool chomp_tokens(const char** tokens, const char** ptr, const char* end, char* buf)
{
    bool next = true;
    bool rval = false;

    do
    {
        next = false;

        for (int i = 0; tokens[i]; i++)
        {
            if (chomp_one_token(tokens[i], ptr, end, buf))
            {
                rval = true;
                next = true;
                break;
            }
        }
    }
    while (next);

    return rval;
}

/**
 * Remove any extra characters from a string
 */
static void remove_extras(char* str)
{
    char* end = strchr(str, '\0') - 1;

    while (end > str && (*end == '`' || *end == ')' || *end == '('))
    {
        *end-- = '\0';
    }

    char* start = str;

    while (start < end && (*start == '`' || *start == ')' || *start == '('))
    {
        start++;
    }

    size_t len = strlen(start);

    memmove(str, start, len);
    str[len] = '\0';

    ss_dassert(strlen(str) == len);
}


static void remove_backticks(char* src)
{
    char* dest = src;

    while (*src)
    {
        if (*src != '`')
        {
            // Non-backtick character, keep it
            *dest = *src;
            dest++;
        }

        src++;
    }

    ss_dassert(dest == src || (*dest != '\0' && dest < src));
    *dest = '\0';
}

/**
 * Extract both tables from a `CREATE TABLE t1 LIKE t2` statement
 */
static bool extract_create_like_identifier(const char* sql, size_t len, char* target, char* source)
{
    bool rval = false;
    char buffer[len + 1];
    buffer[0] = '\0';
    const char* ptr = sql;
    const char* end = ptr + sizeof(buffer);

    if (chomp_tokens(TOK_CREATE, &ptr, end, buffer))
    {
        chomp_tokens(TOK_GROUP_REPLACE, &ptr, end, buffer);

        if (chomp_tokens(TOK_TABLE, &ptr, end, buffer))
        {
            chomp_tokens(TOK_GROUP_EXISTS, &ptr, end, buffer);

            // Read the target table name
            ptr = get_token(ptr, end, buffer);
            strcpy(target, buffer);

            // Skip the LIKE token
            ptr = get_token(ptr, end, buffer);

            // Read the source table name
            ptr = get_token(ptr, end, buffer);
            remove_extras(buffer);
            strcpy(source, buffer);
            rval = true;
        }
    }

    return rval;
}

/**
 * Create a table from another table
 */
TABLE_CREATE* table_create_copy(AVRO_INSTANCE *router, const char* sql, size_t len, const char* db)
{
    TABLE_CREATE* rval = NULL;
    char target[MYSQL_TABLE_MAXLEN + 1] = "";
    char source[MYSQL_TABLE_MAXLEN + 1] = "";

    if (extract_create_like_identifier(sql, len, target, source))
    {
        char table_ident[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 2] = "";

        if (strchr(source, '.') == NULL)
        {
            strcpy(table_ident, db);
            strcat(table_ident, ".");
        }

        strcat(table_ident, source);

        TABLE_CREATE *old = hashtable_fetch(router->created_tables, table_ident);

        if (old)
        {
            int n = old->columns;
            char** names = MXS_MALLOC(sizeof(char*) * n);
            char** types = MXS_MALLOC(sizeof(char*) * n);
            int* lengths = MXS_MALLOC(sizeof(int) * n);
            rval = MXS_MALLOC(sizeof(TABLE_CREATE));

            MXS_ABORT_IF_FALSE(names && types && lengths && rval);

            for (uint64_t i = 0; i < old->columns; i++)
            {
                names[i] = MXS_STRDUP_A(old->column_names[i]);
                types[i] = MXS_STRDUP_A(old->column_types[i]);
                lengths[i] = old->column_lengths[i];
            }

            rval->version = 1;
            rval->was_used = false;
            rval->column_names = names;
            rval->column_lengths = lengths;
            rval->column_types = types;
            rval->columns = old->columns;
            rval->database = MXS_STRDUP_A(db);

            char* table = strchr(target, '.');
            table = table ? table + 1 : target;
            rval->table = MXS_STRDUP_A(table);
        }
        else
        {
            MXS_ERROR("Could not find table '%s' that '%s' is being created from: %.*s",
                      table_ident, target, (int)len, sql);
        }
    }

    return rval;
}

/**
 * Free a TABLE_CREATE structure
 * @param value Value to free
 */
void table_create_free(TABLE_CREATE* value)
{
    if (value)
    {
        for (uint64_t i = 0; i < value->columns; i++)
        {
            MXS_FREE(value->column_names[i]);
            MXS_FREE(value->column_types[i]);
        }
        MXS_FREE(value->column_names);
        MXS_FREE(value->column_types);
        MXS_FREE(value->column_lengths);
        MXS_FREE(value->table);
        MXS_FREE(value->database);
        MXS_FREE(value);
    }
}

static const char* get_next_def(const char* sql, const char* end)
{
    int depth = 0;
    while (sql < end)
    {
        if (*sql == ',' && depth == 0)
        {
            return sql + 1;
        }
        else if (*sql == '(')
        {
            depth++;
        }
        else if (*sql == ')')
        {
            depth--;
        }
        sql++;
    }

    return NULL;
}

static const char* get_tok(const char* sql, int* toklen, const char* end)
{
    const char *start = sql;

    while (isspace(*start))
    {
        start++;
    }

    int len = 0;
    int depth = 0;
    while (start + len < end)
    {
        if (isspace(start[len]) && depth == 0)
        {
            *toklen = len;
            return start;
        }
        else if (start[len] == '(')
        {
            depth++;
        }
        else if (start[len] == ')')
        {
            depth--;
        }

        len++;
    }

    if (len > 0 && start + len <= end)
    {
        *toklen = len;
        return start;
    }

    return NULL;
}

static bool tok_eq(const char *a, const char *b, size_t len)
{
    size_t i = 0;

    while (i < len)
    {
        if (tolower(a[i]) - tolower(b[i]) != 0)
        {
            return false;
        }
        i++;
    }

    return true;
}

void read_alter_identifier(const char *sql, const char *end, char *dest, int size)
{
    int len = 0;
    const char *tok = get_tok(sql, &len, end); // ALTER
    if (tok && (tok = get_tok(tok + len, &len, end)) // TABLE
                && (tok = get_tok(tok + len, &len, end))) // Table identifier
    {
        snprintf(dest, size, "%.*s", len, tok);
        remove_backticks(dest);
    }
}

void make_avro_token(char* dest, const char* src, int length)
{
    while (length > 0 && (*src == '(' || *src == ')' || *src == '`' || isspace(*src)))
    {
        src++;
        length--;
    }

    const char *end = src;

    for (int i = 0; i < length; i++)
    {
        if (end[i] == '(' || end[i] == ')' || end[i] == '`' || isspace(end[i]))
        {
            length = i;
            break;
        }
    }

    memcpy(dest, src, length);
    dest[length] = '\0';
    fix_reserved_word(dest);
}

int get_column_index(TABLE_CREATE *create, const char *tok, int len)
{
    int idx = -1;
    char safe_tok[len + 2];
    memcpy(safe_tok, tok, len);
    safe_tok[len] = '\0';
    fix_reserved_word(safe_tok);

    for (int x = 0; x < create->columns; x++)
    {
        if (strcasecmp(create->column_names[x], safe_tok) == 0)
        {
            idx = x;
            break;
        }
    }

    return idx;
}

bool table_create_alter(TABLE_CREATE *create, const char *sql, const char *end)
{
    const char *tbl = strcasestr(sql, "table"), *def;

    if ((def = strchr(tbl, ' ')))
    {
        int len = 0;
        const char *tok = get_tok(def, &len, end);

        if (tok)
        {
            MXS_INFO("Alter table '%.*s'; %.*s\n", len, tok, (int)(end - sql), sql);
            def = tok + len;
        }

        int updates = 0;

        while (tok && (tok = get_tok(tok + len, &len, end)))
        {
            const char *ptok = tok;
            int plen = len;
            tok = get_tok(tok + len, &len, end);

            if (tok)
            {
                if (tok_eq(ptok, "add", plen) && tok_eq(tok, "column", len))
                {
                    tok = get_tok(tok + len, &len, end);
                    char avro_token[len + 1];
                    make_avro_token(avro_token, tok, len);
                    bool is_new = true;

                    for (uint64_t i = 0; i < create->columns; i++)
                    {
                        if (strcmp(avro_token, create->column_names[i]) == 0)
                        {
                            is_new = false;
                            break;
                        }
                    }

                    if (is_new)
                    {
                        create->column_names = MXS_REALLOC(create->column_names, sizeof(char*) * (create->columns + 1));
                        create->column_types = MXS_REALLOC(create->column_types, sizeof(char*) * (create->columns + 1));
                        create->column_lengths = MXS_REALLOC(create->column_lengths, sizeof(int) * (create->columns + 1));

                        char field_type[200] = ""; // Enough to hold all types
                        int field_length = extract_type_length(tok + len, field_type);
                        create->column_names[create->columns] = MXS_STRDUP_A(avro_token);
                        create->column_types[create->columns] = MXS_STRDUP_A(field_type);
                        create->column_lengths[create->columns] = field_length;
                        create->columns++;
                        updates++;
                    }
                    tok = get_next_def(tok, end);
                    len = 0;
                }
                else if (tok_eq(ptok, "drop", plen) && tok_eq(tok, "column", len))
                {
                    tok = get_tok(tok + len, &len, end);

                    int idx = get_column_index(create, tok, len);

                    if (idx != -1)
                    {
                        MXS_FREE(create->column_names[idx]);
                        MXS_FREE(create->column_types[idx]);
                        for (int i = idx; i < (int)create->columns - 1; i++)
                        {
                            create->column_names[i] = create->column_names[i + 1];
                            create->column_types[i] = create->column_types[i + 1];
                            create->column_lengths[i] = create->column_lengths[i + 1];
                        }

                        create->column_names = MXS_REALLOC(create->column_names, sizeof(char*) * (create->columns - 1));
                        create->column_types = MXS_REALLOC(create->column_types, sizeof(char*) * (create->columns - 1));
                        create->column_lengths = MXS_REALLOC(create->column_lengths, sizeof(int) * (create->columns - 1));
                        create->columns--;
                        updates++;
                    }

                    tok = get_next_def(tok, end);
                    len = 0;
                }
                else if (tok_eq(ptok, "change", plen) && tok_eq(tok, "column", len))
                {
                    tok = get_tok(tok + len, &len, end);

                    int idx = get_column_index(create, tok, len);

                    if (idx != -1 && (tok = get_tok(tok + len, &len, end)))
                    {
                        MXS_FREE(create->column_names[idx]);
                        MXS_FREE(create->column_types[idx]);
                        char avro_token[len + 1];
                        make_avro_token(avro_token, tok, len);
                        char field_type[200] = ""; // Enough to hold all types
                        int field_length = extract_type_length(tok + len, field_type);
                        create->column_names[idx] = MXS_STRDUP_A(avro_token);
                        create->column_types[idx] = MXS_STRDUP_A(field_type);
                        create->column_lengths[idx] = field_length;
                        updates++;
                    }

                    tok = get_next_def(tok, end);
                    len = 0;
                }
            }
            else
            {
                break;
            }
        }

        /** Only increment the create version if it has an associated .avro
         * file. The .avro file is only created if it is actually used. */
        if (updates > 0 && create->was_used)
        {
            create->version++;
            create->was_used = false;
        }
    }

    return true;
}

/**
 * @brief Read the fully qualified name of the table
 *
 * @param ptr Pointer to the start of the event payload
 * @param post_header_len Length of the event specific header, 8 or 6 bytes
 * @param dest Destination where the string is stored
 * @param len Size of destination
 */
void read_table_info(uint8_t *ptr, uint8_t post_header_len, uint64_t *tbl_id, char* dest, size_t len)
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

/**
 * @brief Extract a table map from a table map event
 *
 * This assumes that the complete event minus the replication header is stored
 * at @p ptr
 * @param ptr Pointer to the start of the event payload
 * @param post_header_len Length of the event specific header, 8 or 6 bytes
 * @return New TABLE_MAP or NULL if memory allocation failed
 */
TABLE_MAP *table_map_alloc(uint8_t *ptr, uint8_t hdr_len, TABLE_CREATE* create)
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

    uint64_t column_count = mxs_leint_value(ptr);
    ptr += mxs_leint_bytes(ptr);

    /** Column types */
    uint8_t *column_types = ptr;
    ptr += column_count;

    size_t metadata_size = 0;
    uint8_t* metadata = (uint8_t*)mxs_lestr_consume(&ptr, &metadata_size);
    uint8_t *nullmap = ptr;
    size_t nullmap_size = (column_count + 7) / 8;
    TABLE_MAP *map = MXS_MALLOC(sizeof(TABLE_MAP));

    if (map)
    {
        map->id = table_id;
        map->version = create->version;
        map->flags = flags;
        map->columns = column_count;
        map->column_types = MXS_MALLOC(column_count);
        /** Allocate at least one byte for the metadata */
        map->column_metadata = MXS_CALLOC(1, metadata_size + 1);
        map->column_metadata_size = metadata_size;
        map->null_bitmap = MXS_MALLOC(nullmap_size);
        map->database = MXS_STRDUP(schema_name);
        map->table = MXS_STRDUP(table_name);
        map->table_create = create;
        if (map->column_types && map->database && map->table &&
            map->column_metadata && map->null_bitmap)
        {
            memcpy(map->column_types, column_types, column_count);
            memcpy(map->null_bitmap, nullmap, nullmap_size);
            memcpy(map->column_metadata, metadata, metadata_size);
        }
        else
        {
            MXS_FREE(map->null_bitmap);
            MXS_FREE(map->column_metadata);
            MXS_FREE(map->column_types);
            MXS_FREE(map->database);
            MXS_FREE(map->table);
            MXS_FREE(map);
            map = NULL;
        }
    }

    return map;
}

/**
 * @brief Free a table map
 * @param map Table map to free
 */
void table_map_free(TABLE_MAP *map)
{
    if (map)
    {
        MXS_FREE(map->column_types);
        MXS_FREE(map->database);
        MXS_FREE(map->table);
        MXS_FREE(map);
    }
}
