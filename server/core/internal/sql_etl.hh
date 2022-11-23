/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>

#include <vector>

#include <maxbase/json.hh>
#include <maxbase/ssl.hh>

#include "sql_conn_manager.hh"

//
// Extract, Transform, Load.
// Functionality for importing data from external systems into MariaDB.
//

namespace sql_etl
{

enum class Source
{
    MARIADB
};

struct Error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Config
{
    Config(Source source_type, std::string odbc_src, std::string odbc_dest)
        : type(source_type)
        , src (odbc_src)
        , dest(odbc_dest)
    {
    }

    // The type of the source server
    Source type;

    // The ODBC connection string to the server where the data is read from
    std::string src;

    // The ODBC connection string to the MariaDB server where the data is sent
    std::string dest;
};

class ETL;

class Table
{
public:
    Table(const ETL& etl,
          std::string_view schema,
          std::string_view table,
          std::string_view create,
          std::string_view select,
          std::string_view insert);

    void prepare() noexcept;

    void start() noexcept;

    bool ok() const
    {
        return m_error.empty();
    }

    const std::string& error() const
    {
        return m_error;
    }

    mxb::Json to_json() const;

    const char* table() const
    {
        return m_table.c_str();
    }

    const char* schema() const
    {
        return m_schema.c_str();
    }

private:

    const ETL&  m_etl;
    std::string m_schema;
    std::string m_table;
    std::string m_create;
    std::string m_select;
    std::string m_insert;
    std::string m_error;
};

struct ETL
{
    ETL(Config config)
        : m_config(std::move(config))
    {
    }

    const Config& config() const
    {
        return m_config;
    }

    std::vector<Table>& tables()
    {
        return m_tables;
    }


    mxb::Json prepare();

    mxb::Json start();

private:
    template<auto func>
    mxb::Json run_job();

    Config             m_config;
    std::vector<Table> m_tables;
};

/**
 * Create ETL operation
 *
 * @param json    The JSON needed to configure the operation
 * @param src_cc  The connection configuration to the source server
 * @param dest_cc The connection configuration to the destination server
 *
 * @return The ETL instance if the creation was successful
 *
 * @throws ETLError
 */
std::unique_ptr<ETL> create(const mxb::Json& json,
                            const HttpSql::ConnectionConfig& src_cc,
                            const HttpSql::ConnectionConfig& dest_cc);
}
