/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>

#include <deque>
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

// Helper function for creating exceptions
template<class ... Args>
Error problem(Args&& ... args)
{
    std::ostringstream ss;
    (ss << ... << args);
    return Error(ss.str());
}

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

    // Connection and query timeout
    std::chrono::seconds timeout = 30s;

    // What to do if the table already exists
    enum class CreateMode
    {
        NORMAL,     // CREATE TABLE: causes an error to be reported
        REPLACE,    // CREATE OR REPLACE TABLE: drops the existing table
        IGNORE,     // CREATE TABLE IF NOT EXISTS: ignores the error
    };

    CreateMode create_mode = CreateMode::NORMAL;
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
    virtual void start(mxq::ODBC& source, const std::deque<Table>& tables) = 0;

    /**
     * Called whenever a thread is created for dumping data.
     *
     * The DB given to the function will be the same instance for the whole lifetime of the thread and thus
     * its state does not need to be initialized when the other functions are called.
     *
     * @param source Connection to the source server
     * @param tables The table being imported
     */
    virtual void start_thread(mxq::ODBC& source, const std::deque<Table>& tables) = 0;

    /**
     * Called after data dump is ready to start
     *
     * this function is called for the main coordinating connection after all threads have been
     * successfully started and data dump is ready to start.
     *
     * @param source Connection to the source server
     * @param tables Table that are to be imported
     */
    virtual void threads_started(mxq::ODBC& source, const std::deque<Table>& tables) = 0;

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
     * Should return the SQL for a prepared statment that is used to insert the data into MariaDB.
     *
     * Unlike the create_table() and select() functions, the SQL returned by this function is used to prepare
     * an insert statement and should only contain placeholders. The SQL should be of the form `INSERT INTO
     * table(columns ...) VALUE (?, ?, ...)` and the INSERT must be directly compatible with the resultset of
     * the SELECT statement used to read the data. The field names should be explicitly defined to avoid any
     * problems with generated columns in the middle of the table.
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

    Config::CreateMode create_mode() const;

private:
    void prepare_sql(mxq::ODBC& source, mxq::ODBC& dest);

    ETL&          m_etl;
    std::string   m_schema;
    std::string   m_table;
    std::string   m_create;
    std::string   m_select;
    std::string   m_insert;
    std::string   m_error;
    mxb::Duration m_duration {0};
    int64_t       m_rows{0};

    mutable std::mutex m_lock;
};

class ETL
{
public:
    enum class Stage
    {
        PREPARE,
        CREATE,
        LOAD,
    };

    ETL(std::string_view id, Config config, std::unique_ptr<Extractor> extractor)
        : m_id(id)
        , m_config(std::move(config))
        , m_extractor(std::move(extractor))
        , m_init_latch{(std::ptrdiff_t)m_config.threads}
        , m_create_latch{(std::ptrdiff_t)m_config.threads}
        , m_load_latch{(std::ptrdiff_t)m_config.threads}
    {
    }

    const Config& config() const
    {
        return m_config;
    }

    std::deque<Table>& tables()
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

    void cancel();

    mxb::Json to_json()
    {
        return to_json("");
    }

private:
    template<auto connect_func, auto run_func, auto interrupt_func>
    mxb::Json run_job();
    void      run_prepare_job(mxq::ODBC& source) noexcept;
    void      run_start_job(std::pair<mxq::ODBC, mxq::ODBC>& connections) noexcept;
    mxb::Json to_json(const std::string error);

    mxq::ODBC connect_to_source();
    void      interrupt_source(mxq::ODBC& source);

    std::pair<mxq::ODBC, mxq::ODBC> connect_to_both();
    void                            interrupt_both(std::pair<mxq::ODBC, mxq::ODBC>& connections);

    bool   checkpoint(int* current_checkpoint, Stage stage);
    Table* next_table();

    std::string                m_id;
    Config                     m_config;
    std::deque<Table>          m_tables;
    std::unique_ptr<Extractor> m_extractor;

    std::mutex m_lock;
    bool       m_have_error{false};

    // Protected by m_lock
    std::function<void()> m_interruptor;

    mxb::latch          m_init_latch;
    mxb::latch          m_create_latch;
    mxb::latch          m_load_latch;
    int                 m_next_checkpoint {0};
    Stage               m_stage {Stage::PREPARE};
    std::atomic<size_t> m_counter{0};
};

/**
 * Create ETL operation
 *
 * @param id      The ID for this ETL operation
 * @param json    The JSON needed to configure the operation
 * @param src_cc  The connection configuration to the source server
 * @param dest_cc The connection configuration to the destination server
 *
 * @return The ETL instance if the creation was successful
 *
 * @throws ETLError
 */
std::unique_ptr<ETL> create(std::string_view id, const mxb::Json& json,
                            const HttpSql::ConnectionConfig& src_cc,
                            const HttpSql::ConnectionConfig& dest_cc);

/**
 * Get the correct CREATE TABLE statement for the CreateMode
 *
 * @param mode The CreateMode to use
 *
 * @return The CREATE TABLE statement that corresponds to the mode
 */
std::string_view to_create_table(Config::CreateMode mode);
}
