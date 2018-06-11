#pragma once
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

#include <vector>
#include <cstdint>
#include <string>
#include <sstream>
#include <tr1/memory>
#include <tr1/unordered_map>

#include <maxscale/pcre2.h>
#include <maxscale/service.h>
#include <binlog_common.h>

typedef std::vector<uint8_t> Bytes;

// A GTID position
struct gtid_pos_t
{
    gtid_pos_t():
        timestamp(0),
        domain(0),
        server_id(0),
        seq(0),
        event_num(0)
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

    void extract(const REP_HEADER& hdr, uint8_t* ptr);
    bool parse(const char* str);
    static gtid_pos_t from_string(std::string str);
    std::string to_string() const;
    bool empty() const;
};

/** A single column in a CREATE TABLE statement */
struct Column
{
    Column(std::string name, std::string type = "unknown", int length = -1):
        name(name),
        type(type),
        length(length)
    {
    }

    std::string name;
    std::string type;
    int         length;
};

/** A CREATE TABLE abstraction */
struct TableCreateEvent
{
    TableCreateEvent(std::string db, std::string table, int version, std::vector<Column>&& cols):
        columns(cols),
        table(table),
        database(db),
        version(version),
        was_used(false)
    {
    }

    std::string id() const
    {
        return database + '.' + table;
    }

    std::vector<Column>        columns;
    std::string                table;
    std::string                database;
    int                        version;  /**< How many versions of this table have been used */
    bool                       was_used; /**< Has this schema been persisted to disk */
};

/** A representation of a table map event read from a binary log. A table map
 * maps a table to a unique ID which can be used to match row events to table map
 * events. The table map event tells us how the table is laid out and gives us
 * some meta information on the columns. */
struct TableMapEvent
{
    TableMapEvent(const std::string& db, const std::string& table, uint64_t id,
                  int version, Bytes&& cols, Bytes&& nulls, Bytes&& metadata):
        database(db),
        table(table),
        id(id),
        version(version),
        column_types(cols),
        null_bitmap(nulls),
        column_metadata(metadata)
    {
    }

    uint64_t columns() const
    {
        return column_types.size();
    }

    std::string database;
    std::string table;
    uint64_t    id;
    int         version;
    Bytes       column_types;
    Bytes       null_bitmap;
    Bytes       column_metadata;
};

typedef std::tr1::shared_ptr<TableCreateEvent> STableCreateEvent;
typedef std::tr1::shared_ptr<TableMapEvent>    STableMapEvent;

// Containers for the replication events
typedef std::tr1::unordered_map<std::string, STableCreateEvent> CreatedTables;
typedef std::tr1::unordered_map<std::string, STableMapEvent>    MappedTables;
typedef std::tr1::unordered_map<uint64_t, STableMapEvent>       ActiveMaps;

// Handler class for row based replication events
class RowEventHandler
{
public:
    virtual ~RowEventHandler()
    {
    }

    // A table was opened
    virtual bool open_table(const STableMapEvent& map, const STableCreateEvent& create)
    {
        return true;
    }

    // Prepare a new row for processing
    virtual bool prepare_table(std::string database, std::string table)
    {
        return true;
    }

    // Flush open tables
    virtual void flush_tables()
    {
    }

    // Prepare a new row for processing
    virtual void prepare_row(const gtid_pos_t& gtid, const REP_HEADER& hdr, int event_type) = 0;

    // Called once all columns are processed
    virtual bool commit(const gtid_pos_t& gtid) = 0;

    // 32-bit integer handler
    virtual void column(int i, int32_t value) = 0;

    // 64-bit integer handler
    virtual void column(int i, int64_t value) = 0;

    // Float handler
    virtual void column(int i, float value) = 0;

    // Double handler
    virtual void column(int i, double value) = 0;

    // String handler
    virtual void column(int i, std::string value) = 0;

    // Bytes handler
    virtual void column(int i, uint8_t* value, int len) = 0;

    // Empty (NULL) value type handler
    virtual void column(int i) = 0;
};

typedef std::auto_ptr<RowEventHandler> SRowEventHandler;

class Rpl
{
public:
    Rpl(const Rpl&) = delete;
    Rpl& operator=(const Rpl&) = delete;

    // Construct a new replication stream transformer
    Rpl(SERVICE* service, SRowEventHandler event_handler, gtid_pos_t = {});

    // Add a stored TableCreateEvent
    void add_create(STableCreateEvent create);

    // Handle a replicated binary log event
    void handle_event(REP_HEADER hdr, uint8_t* ptr);

    // Called when processed events need to be persisted to disk
    void flush();

    // Check if binlog checksums are enabled
    bool have_checksums() const
    {
        return m_binlog_checksum;
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

private:
    SRowEventHandler m_handler;
    SERVICE*         m_service;
    pcre2_code*      m_create_table_re;
    pcre2_code*      m_alter_table_re;
    uint8_t          m_binlog_checksum;
    uint8_t          m_event_types;
    Bytes            m_event_type_hdr_lens;
    gtid_pos_t       m_gtid;
    ActiveMaps       m_active_maps;
    MappedTables     m_table_maps;
    CreatedTables    m_created_tables;

    void handle_query_event(REP_HEADER *hdr, uint8_t *ptr);
    bool handle_table_map_event(REP_HEADER *hdr, uint8_t *ptr);
    bool handle_row_event(REP_HEADER *hdr, uint8_t *ptr);
    STableCreateEvent table_create_copy(const char* sql, size_t len, const char* db);
    bool save_and_replace_table_create(STableCreateEvent created);
};
