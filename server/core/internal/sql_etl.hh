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

#include "sql_conn_manager.hh"

//
// Extract, Transform, Load.
// Functionality for importing data from external systems into MariaDB.
//

namespace sql_etl
{

enum class Source
{
    MARIADB,
    UNKNOWN
};

struct Error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Config
{
    // TODO: Implement
};

class ETL;

class Table
{
public:
    Table(const ETL& etl,
          std::string_view schema,
          std::string_view table,
          std::string_view create,
          std::string_view select);

    void read_create() noexcept;

    void read_select() noexcept;

    void start() noexcept;

    bool ok() const
    {
        return m_error.empty();
    }

    const std::string& error() const
    {
        return m_error;
    }

private:

    const ETL&  m_etl;
    std::string m_schema;
    std::string m_table;
    std::string m_create;
    std::string m_select;
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

    mxb::Json list_tables();

    mxb::Json get_create();

    mxb::Json get_select();

    mxb::Json start();

private:
    template<auto func>
    mxb::Json run_job();

    Config             m_config;
    std::vector<Table> m_tables;
};

/**
 * Read configuration from JSON
 *
 * @param json The JSON needed to configure the source server connection
 * @param cc   The connection configuration to the MariaDB server
 *
 * @return The configuration if the JSON was valid
 *
 * @throws ETLError
 */
std::unique_ptr<Config> configure(const mxb::Json& json, const HttpSql::ConnectionConfig& cc);

/**
 * Create ETL from configuration
 *
 * @param config The base ETL connection configuration
 * @param json   The JSON data for the operation being done
 *
 * @return The ETL instance if the creation was successful
 *
 * @throws ETLError
 */
std::unique_ptr<ETL> create(const Config& config, const mxb::Json& json);
}
