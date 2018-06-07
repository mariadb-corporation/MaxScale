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

// Handler class for row based replication events
class RowEventHandler
{
public:
    RowEventHandler(const STableMapEvent& map, const STableCreateEvent& create):
        m_map(map),
        m_create(create)
    {
    }

    virtual ~RowEventHandler()
    {
    }

    // Prepare a new row for processing
    virtual void prepare(const gtid_pos_t& gtid, const REP_HEADER& hdr, int event_type) = 0;

    // Called once all columns are processed
    virtual bool commit() = 0;

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

protected:
    const STableMapEvent&    m_map; // The table map event for this row
    const STableCreateEvent& m_create; // The CREATE TABLE statement for this row
};
