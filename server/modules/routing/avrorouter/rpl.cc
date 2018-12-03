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

#include "avrorouter.hh"
#include "rpl.hh"

#include <sstream>
#include <algorithm>

#include <maxbase/assert.h>
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mysql.h>

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

json_t* Column::to_json() const
{
    json_t* obj = json_object();
    json_object_set_new(obj, "name", json_string(name.c_str()));
    json_object_set_new(obj, "type", json_string(type.c_str()));
    json_object_set_new(obj, "length", json_integer(length));
    return obj;
}

Column Column::from_json(json_t* json)
{
    json_t* name = json_object_get(json, "name");
    json_t* type = json_object_get(json, "type");
    json_t* length = json_object_get(json, "length");

    if (name && json_is_string(name)
        && type && json_is_string(type)
        && length && json_is_integer(length))
    {
        return Column(json_string_value(name),
                      json_string_value(type),
                      json_integer_value(length));
    }

    // Invalid JSON, return empty Column
    return Column("");
}

json_t* TableCreateEvent::to_json() const
{
    json_t* arr = json_array();

    for (auto it = columns.begin(); it != columns.end(); it++)
    {
        json_array_append_new(arr, it->to_json());
    }

    json_t* obj = json_object();
    json_object_set_new(obj, "table", json_string(table.c_str()));
    json_object_set_new(obj, "database", json_string(database.c_str()));
    json_object_set_new(obj, "version", json_integer(version));
    json_object_set_new(obj, "columns", arr);

    return obj;
}

STableCreateEvent TableCreateEvent::from_json(json_t* obj)
{
    STableCreateEvent rval;
    json_t* table = json_object_get(obj, "table");
    json_t* database = json_object_get(obj, "database");
    json_t* version = json_object_get(obj, "version");
    json_t* columns = json_object_get(obj, "columns");

    if (json_is_string(table) && json_is_string(database)
        && json_is_integer(version) && json_is_array(columns))
    {
        std::string tbl = json_string_value(table);
        std::string db = json_string_value(database);
        int ver = json_integer_value(version);
        std::vector<Column> cols;
        size_t i = 0;
        json_t* val;

        json_array_foreach(columns, i, val)
        {
            cols.emplace_back(Column::from_json(val));
        }

        auto is_empty = [](const Column& col) {
                return col.name.empty();
            };

        if (std::none_of(cols.begin(), cols.end(), is_empty))
        {
            rval.reset(new TableCreateEvent(db, tbl, ver, std::move(cols)));
        }
    }

    return rval;
}

/**
 * Extract the table definition from a CREATE TABLE statement
 * @param sql The SQL statement
 * @param size Length of the statement
 * @return Pointer to the start of the definition of NULL if the query is
 * malformed.
 */
static const char* get_table_definition(const char* sql, int len, int* size)
{
    const char* rval = NULL;
    const char* ptr = sql;
    const char* end = sql + len;
    while (ptr < end && *ptr != '(')
    {
        ptr++;
    }

    /** We assume at least the parentheses are in the statement */
    if (ptr < end - 2)
    {
        int depth = 0;
        ptr++;
        const char* start = ptr;    // Skip first parenthesis
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
        while (ptr >= sql && (*ptr == '`' || isspace(*ptr)))
        {
            ptr--;
        }

        while (ptr >= sql && *ptr != '`' && *ptr != '.' && !isspace(*ptr))
        {
            ptr--;
        }

        while (ptr >= sql && (*ptr == '`' || isspace(*ptr)))
        {
            ptr--;
        }

        if (ptr >= sql && *ptr == '.')
        {
            // The query defines an explicit database

            while (ptr >= sql && (*ptr == '`' || *ptr == '.' || isspace(*ptr)))
            {
                ptr--;
            }

            const char* end = ptr + 1;

            while (ptr >= sql && *ptr != '`' && *ptr != '.' && !isspace(*ptr))
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

static const char* extract_field_name(const char* ptr, char* dest, size_t size)
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
        if (strncasecmp(ptr, "constraint", 10) == 0 || strncasecmp(ptr, "index", 5) == 0
            || strncasecmp(ptr, "key", 3) == 0 || strncasecmp(ptr, "fulltext", 8) == 0
            || strncasecmp(ptr, "spatial", 7) == 0 || strncasecmp(ptr, "foreign", 7) == 0
            || strncasecmp(ptr, "unique", 6) == 0 || strncasecmp(ptr, "primary", 7) == 0)
        {
            // Found a keyword
            return NULL;
        }
    }

    const char* start = ptr;

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
        mxb_assert(strlen(dest) > 0);
    }
    else
    {
        ptr = NULL;
    }

    return ptr;
}

int extract_type_length(const char* ptr, char* dest)
{
    /** Skip any leading whitespace */
    while (*ptr && (isspace(*ptr) || *ptr == '`'))
    {
        ptr++;
    }

    /** The field type definition starts here */
    const char* start = ptr;

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

    int rval = -1;      // No length defined

    /** Start of length definition */
    if (*ptr == '(')
    {
        ptr++;
        char* end;
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
static void process_column_definition(const char* nameptr, std::vector<Column>& columns)
{
    char colname[512];

    while ((nameptr = extract_field_name(nameptr, colname, sizeof(colname))))
    {
        char type[100] = "";
        int len = extract_type_length(nameptr, type);
        nameptr = next_field_definition(nameptr);
        fix_reserved_word(colname);
        columns.emplace_back(colname, type, len);
    }
}

int resolve_table_version(const char* db, const char* table)
{
    int version = 0;
    char buf[PATH_MAX + 1];

    do
    {
        version++;
        snprintf(buf, sizeof(buf), "%s.%s.%06d.avsc", db, table, version);
    }
    while (access(buf, F_OK) == 0);

    return version;
}

/**
 * @brief Handle a query event which contains a CREATE TABLE statement
 *
 * @param ident Table identifier in database.table format
 * @param sql   The CREATE TABLE statement
 * @param len   Length of @c sql
 *
 * @return New CREATE_TABLE object or NULL if an error occurred
 */
STableCreateEvent table_create_alloc(char* ident, const char* sql, int len)
{
    /** Extract the table definition so we can get the column names from it */
    int stmt_len = 0;
    const char* statement_sql = get_table_definition(sql, len, &stmt_len);
    mxb_assert(statement_sql);

    char* tbl_start = strchr(ident, '.');
    mxb_assert(tbl_start);
    *tbl_start++ = '\0';

    char table[MYSQL_TABLE_MAXLEN + 1];
    char database[MYSQL_DATABASE_MAXLEN + 1];
    strcpy(database, ident);
    strcpy(table, tbl_start);

    std::vector<Column> columns;
    process_column_definition(statement_sql, columns);

    STableCreateEvent rval;

    if (!columns.empty())
    {
        int version = resolve_table_version(database, table);
        rval.reset(new(std::nothrow) TableCreateEvent(database, table, version, std::move(columns)));
    }
    else
    {
        MXS_ERROR("No columns in a CREATE TABLE statement: %.*s", stmt_len, statement_sql);
    }

    return rval;
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
TableMapEvent* table_map_alloc(uint8_t* ptr, uint8_t hdr_len, TableCreateEvent* create)
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
    uint8_t* column_types = ptr;
    ptr += column_count;

    size_t metadata_size = 0;
    uint8_t* metadata = (uint8_t*)mxs_lestr_consume(&ptr, &metadata_size);
    uint8_t* nullmap = ptr;
    size_t nullmap_size = (column_count + 7) / 8;

    Bytes cols(column_types, column_types + column_count);
    Bytes nulls(nullmap, nullmap + nullmap_size);
    Bytes meta(metadata, metadata + metadata_size);
    return new(std::nothrow) TableMapEvent(schema_name,
                                           table_name,
                                           table_id,
                                           create->version,
                                           std::move(cols),
                                           std::move(nulls),
                                           std::move(meta));
}

Rpl::Rpl(SERVICE* service,
         SRowEventHandler handler,
         pcre2_code* match,
         pcre2_code* exclude,
         gtid_pos_t  gtid)
    : m_handler(handler)
    , m_service(service)
    , m_binlog_checksum(0)
    , m_event_types(0)
    , m_gtid(gtid)
    , m_match(match)
    , m_exclude(exclude)
    , m_md_match(m_match ? pcre2_match_data_create_from_pattern(m_match, NULL) : nullptr)
    , m_md_exclude(m_exclude ? pcre2_match_data_create_from_pattern(m_exclude, NULL) : nullptr)
{
    /** For detection of CREATE/ALTER TABLE statements */
    static const char* create_table_regex = "(?i)^[[:space:]]*create[a-z0-9[:space:]_]+table";
    static const char* alter_table_regex = "(?i)^[[:space:]]*alter[[:space:]]+table";
    int pcreerr;
    size_t erroff;
    m_create_table_re = pcre2_compile((PCRE2_SPTR) create_table_regex,
                                      PCRE2_ZERO_TERMINATED,
                                      0,
                                      &pcreerr,
                                      &erroff,
                                      NULL);
    m_alter_table_re = pcre2_compile((PCRE2_SPTR) alter_table_regex,
                                     PCRE2_ZERO_TERMINATED,
                                     0,
                                     &pcreerr,
                                     &erroff,
                                     NULL);
    mxb_assert_message(m_create_table_re && m_alter_table_re,
                       "CREATE TABLE and ALTER TABLE regex compilation should not fail");
}

void Rpl::flush()
{
    m_handler->flush_tables();
}

void Rpl::add_create(STableCreateEvent create)
{
    auto it = m_created_tables.find(create->id());

    if (it == m_created_tables.end() || create->version > it->second->version)
    {
        m_created_tables[create->id()] = create;
    }
}

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

    mxb_assert(strlen(str) == len);
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

    mxb_assert(dest == src || (*dest != '\0' && dest < src));
    *dest = '\0';
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
STableCreateEvent Rpl::table_create_copy(const char* sql, size_t len, const char* db)
{
    STableCreateEvent rval;
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

        auto it = m_created_tables.find(table_ident);

        if (it != m_created_tables.end())
        {
            rval.reset(new(std::nothrow) TableCreateEvent(*it->second));
            char* table = strchr(target, '.');
            table = table ? table + 1 : target;
            rval->table = table;
            rval->version = 1;
            rval->was_used = false;
        }
        else
        {
            MXS_ERROR("Could not find table '%s' that '%s' is being created from: %.*s",
                      table_ident,
                      target,
                      (int)len,
                      sql);
        }
    }

    return rval;
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
    const char* start = sql;

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

static void rskip_whitespace(const char* sql, const char** end)
{
    const char* ptr = *end;

    while (ptr > sql && isspace(*ptr))
    {
        ptr--;
    }

    *end = ptr;
}

static void rskip_token(const char* sql, const char** end)
{
    const char* ptr = *end;

    while (ptr > sql && !isspace(*ptr))
    {
        ptr--;
    }

    *end = ptr;
}

static bool get_placement_specifier(const char* sql, const char* end, const char** tgt, int* tgt_len)
{
    bool rval = false;
    mxb_assert(end > sql);
    end--;

    *tgt = NULL;
    *tgt_len = 0;

    // Skip any trailing whitespace
    rskip_whitespace(sql, &end);

    if (*end == '`')
    {
        // Identifier, possibly AFTER `column`
        const char* id_end = end;
        end--;

        while (end > sql && *end != '`')
        {
            end--;
        }

        const char* id_start = end + 1;
        mxb_assert(*end == '`' && *id_end == '`');

        end--;

        rskip_whitespace(sql, &end);
        rskip_token(sql, &end);

        // end points to the character _before_ the token
        end++;

        if (strncasecmp(end, "AFTER", 5) == 0)
        {
            // This column comes after the specified column
            rval = true;
            *tgt = id_start;
            *tgt_len = id_end - id_start;
        }
    }
    else
    {
        // Something else, possibly FIRST or un-backtick'd AFTER
        const char* id_end = end + 1;   // Points to either a trailing space or one-after-the-end
        rskip_token(sql, &end);

        // end points to the character _before_ the token
        end++;

        if (strncasecmp(end, "FIRST", 5) == 0)
        {
            // Put this column first
            rval = true;
        }
        else
        {
            const char* id_start = end + 1;

            // Skip the whitespace and until the start of the current token
            rskip_whitespace(sql, &end);
            rskip_token(sql, &end);

            // end points to the character _before_ the token
            end++;

            if (strncasecmp(end, "AFTER", 5) == 0)
            {
                // This column comes after the specified column
                rval = true;
                *tgt = id_start;
                *tgt_len = id_end - id_start;
            }
        }
    }

    return rval;
}

static bool tok_eq(const char* a, const char* b, size_t len)
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

static void skip_whitespace(const char** saved)
{
    const char* ptr = *saved;

    while (*ptr && isspace(*ptr))
    {
        ptr++;
    }

    *saved = ptr;
}

static void skip_token(const char** saved)
{
    const char* ptr = *saved;

    while (*ptr && !isspace(*ptr) && *ptr != '(' && *ptr != '.')
    {
        ptr++;
    }

    *saved = ptr;
}

static void skip_non_backtick(const char** saved)
{
    const char* ptr = *saved;

    while (*ptr && *ptr != '`')
    {
        ptr++;
    }

    *saved = ptr;
}

const char* keywords[] =
{
    "CREATE",
    "DROP",
    "ALTER",
    "IF",
    "EXISTS",
    "REPLACE",
    "OR",
    "TABLE",
    "NOT",
    NULL
};

static bool token_is_keyword(const char* tok, int len)
{
    for (int i = 0; keywords[i]; i++)
    {
        if (strncasecmp(keywords[i], tok, len) == 0)
        {
            return true;
        }
    }

    return false;
}

void read_table_identifier(const char* db, const char* sql, const char* end, char* dest, int size)
{
    const char* start;
    int len = 0;
    bool is_keyword = true;

    while (is_keyword)
    {
        skip_whitespace(&sql);      // Leading whitespace

        if (*sql == '`')
        {
            // Quoted identifier, not a keyword
            is_keyword = false;
            sql++;
            start = sql;
            skip_non_backtick(&sql);
            len = sql - start;
            sql++;
        }
        else
        {
            start = sql;
            skip_token(&sql);
            len = sql - start;
            is_keyword = token_is_keyword(start, len);
        }
    }

    skip_whitespace(&sql);      // Space after first identifier

    if (*sql != '.')
    {
        // No explicit database
        snprintf(dest, size, "%s.%.*s", db, len, start);
    }
    else
    {
        // Explicit database, skip the period
        sql++;
        skip_whitespace(&sql);      // Space after first identifier

        const char* id_start;
        int id_len = 0;

        if (*sql == '`')
        {
            sql++;
            id_start = sql;
            skip_non_backtick(&sql);
            id_len = sql - id_start;
            sql++;
        }
        else
        {
            id_start = sql;
            skip_token(&sql);
            id_len = sql - id_start;
        }

        snprintf(dest, size, "%.*s.%.*s", len, start, id_len, id_start);
    }
}

void make_avro_token(char* dest, const char* src, int length)
{
    while (length > 0 && (*src == '(' || *src == ')' || *src == '`' || isspace(*src)))
    {
        src++;
        length--;
    }

    const char* end = src;

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

static bool not_column_operation(const char* tok, int len)
{
    const char* keywords[] =
    {
        "PRIMARY",
        "UNIQUE",
        "FULLTEXT",
        "SPATIAL",
        "PERIOD",
        "PRIMARY",
        "KEY",
        "KEYS",
        "INDEX",
        "FOREIGN",
        "CONSTRAINT",
        NULL
    };

    for (int i = 0; keywords[i]; i++)
    {
        if (tok_eq(tok, keywords[i], strlen(keywords[i])))
        {
            return true;
        }
    }

    return false;
}

bool Rpl::table_create_alter(STableCreateEvent create, const char* sql, const char* end)
{
    const char* tbl = strcasestr(sql, "table"), * def;

    if ((def = strchr(tbl, ' ')))
    {
        int len = 0;
        const char* tok = get_tok(def, &len, end);

        if (tok)
        {
            MXS_INFO("Alter table '%.*s'; %.*s\n", len, tok, (int)(end - sql), sql);
            def = tok + len;
        }

        int updates = 0;

        while (tok && (tok = get_tok(tok + len, &len, end)))
        {
            const char* ptok = tok;
            int plen = len;
            tok = get_tok(tok + len, &len, end);

            if (tok)
            {
                if (not_column_operation(tok, len))
                {
                    MXS_INFO("Statement doesn't affect columns, not processing: %s", sql);
                    return true;
                }
                else if (tok_eq(tok, "column", len))
                {
                    // Skip the optional COLUMN keyword
                    tok = get_tok(tok + len, &len, end);
                }

                char avro_token[len + 1];
                make_avro_token(avro_token, tok, len);

                if (tok_eq(ptok, "add", plen))
                {

                    bool is_new = true;

                    for (auto it = create->columns.begin(); it != create->columns.end(); it++)
                    {
                        if (it->name == avro_token)
                        {
                            is_new = false;
                            break;
                        }
                    }

                    if (is_new)
                    {
                        char field_type[200] = "";      // Enough to hold all types
                        int field_length = extract_type_length(tok + len, field_type);
                        create->columns.emplace_back(std::string(avro_token),
                                                     std::string(field_type),
                                                     field_length);
                        updates++;
                    }
                    tok = get_next_def(tok, end);
                    len = 0;
                }
                else if (tok_eq(ptok, "drop", plen))
                {
                    for (auto it = create->columns.begin(); it != create->columns.end(); it++)
                    {
                        if (it->name == avro_token)
                        {
                            create->columns.erase(it);
                            break;
                        }
                    }

                    updates++;

                    tok = get_next_def(tok, end);
                    len = 0;
                }
                else if (tok_eq(ptok, "change", plen))
                {
                    for (auto it = create->columns.begin(); it != create->columns.end(); it++)
                    {
                        if (it->name == avro_token)
                        {
                            if ((tok = get_tok(tok + len, &len, end)))
                            {
                                char avro_token[len + 1];
                                make_avro_token(avro_token, tok, len);
                                char field_type[200] = "";      // Enough to hold all types
                                int field_length = extract_type_length(tok + len, field_type);
                                it->name = avro_token;
                                it->type = field_type;
                                it->length = field_length;
                                updates++;
                            }
                        }
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

            /**
             * Although the table was only altered, we can treat it like it
             * would have been just created. This allows for a minimal API that
             * only creates and replaces tables.
             *
             * TODO: Add DROP TABLE entry point for pruning old tables
             */
            m_handler->create_table(create);
        }
    }

    return true;
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
