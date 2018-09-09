/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include "avrorouter.hh"
#include "rpl.hh"

#include <avro.h>

struct AvroTable
{
    AvroTable(avro_file_writer_t file, avro_value_iface_t* iface, avro_schema_t schema)
        : avro_file(file)
        , avro_writer_iface(iface)
        , avro_schema(schema)
    {
    }

    ~AvroTable()
    {
        avro_file_writer_flush(avro_file);
        avro_file_writer_close(avro_file);
        avro_value_iface_decref(avro_writer_iface);
        avro_schema_decref(avro_schema);
    }

    avro_file_writer_t  avro_file;          /*< Current Avro data file */
    avro_value_iface_t* avro_writer_iface;  /*< Avro C API writer interface */
    avro_schema_t       avro_schema;        /*< Native Avro schema of the table */
};

typedef std::shared_ptr<AvroTable>                  SAvroTable;
typedef std::unordered_map<std::string, SAvroTable> AvroTables;

// Converts replicated events into CDC events
class AvroConverter : public RowEventHandler
{
public:

    AvroConverter(std::string avrodir, uint64_t block_size, mxs_avro_codec_type codec);
    bool open_table(const STableMapEvent& map, const STableCreateEvent& create);
    bool prepare_table(std::string database, std::string table);
    void flush_tables();
    void prepare_row(const gtid_pos_t& gtid, const REP_HEADER& hdr, int event_type);
    bool commit(const gtid_pos_t& gtid);
    void column(int i, int32_t value);
    void column(int i, int64_t value);
    void column(int i, float value);
    void column(int i, double value);
    void column(int i, std::string value);
    void column(int i, uint8_t* value, int len);
    void column(int i);

private:
    avro_value_iface_t* m_writer_iface;
    avro_file_writer_t* m_avro_file;
    avro_value_t        m_record;
    avro_value_t        m_field;
    std::string         m_avrodir;
    AvroTables          m_open_tables;
    uint64_t            m_block_size;
    mxs_avro_codec_type m_codec;
    STableMapEvent      m_map;
    STableCreateEvent   m_create;

    void set_active(int i);
};
