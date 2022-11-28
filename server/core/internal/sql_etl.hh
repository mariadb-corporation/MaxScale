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
#include <mutex>

#include <maxbase/json.hh>
#include <maxbase/ssl.hh>
#include <maxbase/latch.hh>

#include "sql_conn_manager.hh"

//
// Extract, Transform, Load.
// Functionality for importing data from external systems into MariaDB.
//

namespace sql_etl
{

struct Error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Config
{
    Config(std::string odbc_src, std::string odbc_dest)
        : src(odbc_src)
        , dest(odbc_dest)
    {
    }

    // The ODBC connection string to the server where the data is read from
    std::string src;

    // The ODBC connection string to the MariaDB server where the data is sent
    std::string dest;

    // How many threads are used to do the data dump.
    size_t threads = 1;
};

class Table;

// Abstract base class for dump thread synchronization and SQL translations into MariaDB syntax.
// If an error occurs at any point, the base class should throw an sql_etl::Error exception.
struct Extractor
{
    virtual ~Extractor() = default;

    /**
     *  Prepares a connection for use
     *
     * Used to initialize the session state of all connections. Called once for each ODBC connection before
     * any other functions are called.
     *
     * @param source Connection to the source server
     */
    virtual void init_connection(mxq::ODBC& source) = 0;

    /**
     * Called when the data dump is first started and before any threads have been created.
     *
     * @param source Connection to the source server
     * @param tables Table that are to be imported
     * */
    virtual void start(mxq::ODBC& source, const std::vector<Table>& tables) = 0;

    /**
     * Called whenever a thread is created for dumping data.
     *
     * The DB given to the function will be the same instance for the whole lifetime of the thread and thus
     * its state does not need to be initialized when the other functions are called.
     *
     * @param source Connection to the source server
     * @param tables The table being imported
     */
    virtual void start_thread(mxq::ODBC& source, const std::vector<Table>& tables) = 0;

    /**
     * Called after data dump is ready to start
     *
     * this function is called for for the main coordinating connection after all threads have been
     * successfully started and data dump is ready to start.
     *
     * @param source Connection to the source server
     * @param tables Table that are to be imported
     */
    virtual void threads_started(mxq::ODBC& source, const std::vector<Table>& tables) = 0;

    /**
     * Get the CREATE TABLE SQL for the given table
     *
     * @param source Connection to the source server
     * @param tables The table being imported
     *
     * @return The SQL statement needed to create the table. Must be MariaDB-compatible SQL.
     */
    virtual std::string create_table(mxq::ODBC& source, const Table& table) = 0;

    /**
     * Should return the SQL needed to read the data from the source.
     *
     * The statement is almost always a SELECT statement of some sort.
     *
     * @param source Connection to the source server
     * @param tables The table being imported
     *
     * @return The SQL needed to read the data. This must be in the native format of the source server.
     */
    virtual std::string select(mxq::ODBC& source, const Table& table) = 0;


    /**
     * Should return the SQL needed to insert the data into MariaDB.
     *
     * The SQL must be of the form `INSERT INTO table(columns ...) VALUE (values...)` and the INSERT must be
     * directly compatible with the resultset of the SELECT statement used to read the data.
     *
     * @param source Connection to the source server
     * @param tables The table being imported
     *
     * @return The SQL needed to insert the data. Must be MariaDB-compatible SQL.
     */
    virtual std::string insert(mxq::ODBC& source, const Table& table) = 0;
};

class ETL;

class Table
{
public:
    Table(ETL& etl,
          std::string_view schema,
          std::string_view table,
          std::string_view create,
          std::string_view select,
          std::string_view insert);

    void read_sql(mxq::ODBC& source);

    void create_objects(mxq::ODBC& source, mxq::ODBC& dest);

    void load_data(mxq::ODBC& source, mxq::ODBC& dest);

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
    ETL&        m_etl;
    std::string m_schema;
    std::string m_table;
    std::string m_create;
    std::string m_select;
    std::string m_insert;
    std::string m_error;
};

struct ETL
{
    ETL(Config config, std::unique_ptr<Extractor> extractor)
        : m_config(std::move(config))
        , m_extractor(std::move(extractor))
        , m_init_latch{(std::ptrdiff_t)m_config.threads}
        , m_create_latch{(std::ptrdiff_t)m_config.threads}
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

    Extractor& extractor() const
    {
        mxb_assert(m_extractor.get());
        return *m_extractor;
    }

    mxb::Json prepare();

    mxb::Json start();

    void add_error();

private:
    template<auto connect_func, auto run_func>
    mxb::Json run_job();
    void      run_prepare_job(mxq::ODBC& source) noexcept;
    void      run_start_job(std::pair<mxq::ODBC, mxq::ODBC>& connections) noexcept;

    mxq::ODBC                       connect_to_source();
    std::pair<mxq::ODBC, mxq::ODBC> connect_to_both();

    bool   checkpoint(int* current_checkpoint);
    Table* next_table();

    Config                     m_config;
    std::vector<Table>         m_tables;
    std::unique_ptr<Extractor> m_extractor;

    std::mutex m_lock;
    bool       m_have_error{false};

    mxb::latch          m_init_latch;
    mxb::latch          m_create_latch;
    int                 m_next_checkpoint {0};
    std::atomic<size_t> m_counter{0};
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
