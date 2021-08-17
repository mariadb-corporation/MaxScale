/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <sstream>
#include <memory>
#include <unordered_map>
#include <exception>

#include <blr_constants.hh>

#include <maxscale/pcre2.hh>
#include <maxscale/service.hh>

#include "tokenizer.hh"
#include "config.hh"

static const char* avro_domain = "domain";
static const char* avro_server_id = "server_id";
static const char* avro_sequence = "sequence";
static const char* avro_event_number = "event_number";
static const char* avro_event_type = "event_type";
static const char* avro_timestamp = "timestamp";


static inline bool is_reserved_word(const char* word)
{
    return strcasecmp(word, avro_domain) == 0
           || strcasecmp(word, avro_server_id) == 0
           || strcasecmp(word, avro_sequence) == 0
           || strcasecmp(word, avro_event_number) == 0
           || strcasecmp(word, avro_event_type) == 0
           || strcasecmp(word, avro_timestamp) == 0;
}

static inline void fix_reserved_word(char* tok)
{
    if (is_reserved_word(tok))
    {
        strcat(tok, "_");
    }
}

typedef std::vector<uint8_t> Bytes;

// Packet header for replication messages
struct REP_HEADER
{
    int      payload_len;   /*< Payload length (24 bits) */
    uint8_t  seqno;         /*< Response sequence number */
    uint8_t  ok;            /*< OK Byte from packet */
    uint32_t timestamp;     /*< Timestamp - start of binlog record */
    uint8_t  event_type;    /*< Binlog event type */
    uint32_t serverid;      /*< Server id of master */
    uint32_t event_size;    /*< Size of header, post-header and body */
    uint32_t next_pos;      /*< Position of next event */
    uint16_t flags;         /*< Event flags */
};

// A GTID position
struct gtid_pos_t
{
    gtid_pos_t()
        : timestamp(0)
        , domain(0)
        , server_id(0)
        , seq(0)
        , event_num(0)
    {
    }

    uint32_t timestamp; /*< GTID event timestamp */
    uint64_t domain;    /*< Replication domain */
    uint64_t server_id; /*< Server ID */
    uint64_t seq;       /*< Sequence number */
    uint64_t event_num; /*< Subsequence number, increases monotonically. This
                         * is an internal representation of the position of
                         * an event inside a GTID event and it is used to
                         * rebuild GTID events in the correct order. */

    void              extract(const REP_HEADER& hdr, uint8_t* ptr);
    bool              parse(const char* str);
    static gtid_pos_t from_string(std::string str);
    std::string       to_string() const;
    bool              empty() const;
};

/** A single column in a CREATE TABLE statement */
struct Column
{
    Column(std::string name, std::string type = "unknown", int length = -1, bool is_unsigned = false)
        : name(name)
        , type(type)
        , length(length)
        , is_unsigned(is_unsigned)
    {
    }

    std::string name;
    std::string type;
    int         length;
    bool        is_unsigned;
    bool        first = false;
    std::string after;
};

struct Table;
using STable = std::shared_ptr<Table>;

/** A CREATE TABLE abstraction */
struct Table
{
    Table(std::string db, std::string table, int version, std::vector<Column>&& cols, const gtid_pos_t& g)
        : columns(cols)
        , table(table)
        , database(db)
        , version(version)
        , is_open(false)
        , gtid(g)
    {
    }

    // Deserialize from JSON file
    static STable deserialize(const char* filename);

    // Serialize to file as JSON
    void serialize(const char* filename) const;

    // Serialize to JSON
    json_t* to_json() const;

    /**
     * Get the table identifier i.e. `database.table`
     *
     * @return The table identifier
     */
    std::string id() const
    {
        return database + '.' + table;
    }

    uint64_t map_table(uint8_t* ptr, uint8_t hdr_len);

    std::vector<Column> columns;
    std::string         table;
    std::string         database;
    int                 version;    /**< How many versions of this table have been used */
    bool                is_open;    /**< Has this table been opened by the handler */
    gtid_pos_t          gtid;

    Bytes column_types;
    Bytes null_bitmap;
    Bytes column_metadata;
};

// Containers for the replication events
typedef std::unordered_map<std::string, STable> CreatedTables;
typedef std::unordered_map<uint64_t, STable>    ActiveMaps;

// Row event types that map to INSERT, UPDATE and DELETE
enum class RowEvent
{
    WRITE,          // A row was added
    UPDATE,         // The before image of a row
    UPDATE_AFTER,   // The after image of a row
    DELETE,         // The row that was deleted

    UNKNOWN,        // This is never returned
};

// Handler class for row based replication events
class RowEventHandler
{
public:
    virtual ~RowEventHandler() = default;

    // Optional method for loading the GTID position from a custom storage
    virtual gtid_pos_t load_latest_gtid()
    {
        return gtid_pos_t();
    }

    // A table was created or altered
    virtual bool create_table(const Table& create) = 0;

    // A table was used for the first time
    virtual bool open_table(const Table& create) = 0;

    // Prepare a table for row processing
    virtual bool prepare_table(const Table& create) = 0;

    // Flush open tables
    virtual void flush_tables() = 0;

    // Prepare a new row for processing
    virtual void prepare_row(const Table& create, const gtid_pos_t& gtid,
                             const REP_HEADER& hdr, RowEvent event_type) = 0;

    // Called once all columns are processed
    virtual bool commit(const Table& create, const gtid_pos_t& gtid) = 0;

    // Integer handler for short types (less than 32 bits)
    virtual void column_int(const Table& create, int i, int32_t value) = 0;

    // Integer handler for long integer types
    virtual void column_long(const Table& create, int i, int64_t value) = 0;

    // Float handler
    virtual void column_float(const Table& create, int i, float value) = 0;

    // Double handler
    virtual void column_double(const Table& create, int i, double value) = 0;

    // String handler
    virtual void column_string(const Table& create, int i, const std::string& value) = 0;

    // Bytes handler
    virtual void column_bytes(const Table& create, int i, uint8_t* value, int len) = 0;

    // Empty (NULL) value type handler
    virtual void column_null(const Table& create, int i) = 0;
};

using SRowEventHandler = std::unique_ptr<RowEventHandler>;

class Rpl
{
public:
    class ParsingError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    Rpl(const Rpl&) = delete;
    Rpl& operator=(const Rpl&) = delete;

    // Construct a new replication stream transformer
    Rpl(SERVICE* service,
        SRowEventHandler event_handler,
        pcre2_code* match,
        pcre2_code* exclude,
        gtid_pos_t = {});

    // Sets the data directory and loads metadata from disk
    void load_metadata(const std::string& datadir);

    // Handle a replicated binary log event
    void handle_event(REP_HEADER hdr, uint8_t* ptr);

    // Called when processed events need to be persisted to disk
    void flush();

    // Check if binlog checksums are enabled
    bool have_checksums() const
    {
        return m_binlog_checksum;
    }

    // Sets the current server where events are being replicated from. Used to fetch CREATE TABLE statements
    // if TABLE_MAP events are read before the DDL is processed.
    void set_server(const cdc::Server& server)
    {
        m_server = server;
    }

    // Set current GTID
    void set_gtid(gtid_pos_t gtid)
    {
        m_gtid = gtid;
    }

    // Get current GTID
    const gtid_pos_t& get_gtid() const
    {
        return m_gtid;
    }

    // Load GTID from the handler
    gtid_pos_t load_gtid()
    {
        return m_handler->load_latest_gtid();
    }

private:
    SRowEventHandler  m_handler;
    SERVICE*          m_service;
    uint8_t           m_binlog_checksum;
    uint8_t           m_event_types;
    Bytes             m_event_type_hdr_lens;
    gtid_pos_t        m_gtid;
    ActiveMaps        m_active_maps;
    CreatedTables     m_created_tables;
    pcre2_code*       m_match;
    pcre2_code*       m_exclude;
    pcre2_match_data* m_md_match;
    pcre2_match_data* m_md_exclude;
    std::string       m_datadir;
    cdc::Server       m_server;

    std::unordered_map<std::string, int> m_versions;    // Table version numbers per identifier

    void handle_query_event(REP_HEADER* hdr, uint8_t* ptr);
    bool handle_table_map_event(REP_HEADER* hdr, uint8_t* ptr);
    bool handle_row_event(REP_HEADER* hdr, uint8_t* ptr);
    void save_and_replace_table_create(const STable& created);
    void rename_table_create(const STable& created, const std::string& old_id);
    bool table_matches(const std::string& ident);

    uint8_t* process_row_event_data(const Table& create, uint8_t* ptr, uint8_t* columns_present,
                                    uint8_t* end);

    // SQL parsing related variables and methods
    struct
    {
        std::string           db;
        std::string           table;
        tok::Tokenizer::Chain tokens;
    } parser;

    // The main parsing function
    void parse_sql(const std::string& sql, const std::string& db);

    // Utility functions used by the parser
    tok::Type             next();
    tok::Tokenizer::Token chomp();
    tok::Tokenizer::Token assume(tok::Type t);
    bool                  expect(const std::vector<tok::Type>&);
    void                  discard(const std::unordered_set<int>& types);

    // Methods that define the grammar
    void   table_identifier();
    void   parentheses();
    Column column_def();
    void   create_table();
    void   drop_table();
    void   alter_table();
    void   alter_table_add_column(const STable& create);
    void   alter_table_drop_column(const STable& create);
    void   alter_table_modify_column(const STable& create);
    void   alter_table_change_column(const STable& create);
    void   rename_table();

    // Non-parsing methods called by the parser
    void do_create_table();
    void do_create_table_like(const std::string& old_db, const std::string& old_table,
                              const std::string& new_db, const std::string& new_table);
    void do_table_rename(const std::string& old_db, const std::string& old_table,
                         const std::string& new_db, const std::string& new_table);
    void do_add_column(const STable& create, Column c);
    void do_drop_column(const STable& create, const std::string& name);
    void do_change_column(const STable& create, const std::string& old_name);
};
