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
