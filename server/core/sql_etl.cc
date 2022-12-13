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

#include "internal/sql_etl.hh"
#include "internal/sql_etl_generic.hh"
#include "internal/servermanager.hh"
#include <maxbase/format.hh>

namespace
{
using Type = mxb::Json::Type;
using namespace sql_etl;

mxb::Json maybe_get(const mxb::Json& json, const std::string& path, mxb::Json::Type type)
{
    auto elem = json.at(path);
    return elem.type() == type ? elem : mxb::Json(type);
}

mxb::Json get(const mxb::Json& json, const std::string& path, mxb::Json::Type type)
{
    auto elem = json.at(path);

    if (elem.type() != type)
    {
        throw problem("Value at '", path, "' is '", elem.type(), "', expected '", type, "'");
    }

    return elem;
}

std::string field_from_result(mxq::ODBC& source, const std::string& sql, int field)
{
    std::string rval;
    mxq::TextResult textresult;

    if (source.query(sql, &textresult))
    {
        if (auto val = textresult.get_field(field))
        {
            rval = *val;
        }
        else
        {
            mxb_assert_message(!true, "Query did not return a result: %s", sql.c_str());
            throw problem("Unexpected empty result");
        }
    }
    else
    {
        throw problem(source.error());
    }

    return rval;
}
}

namespace sql_etl
{
class MariaDBExtractor final : public Extractor
{
public:
    void init_connection(mxq::ODBC& source) override
    {
        if (!source.query("SET AUTOCOMMIT=0,SQL_MODE='PIPES_AS_CONCAT,NO_ENGINE_SUBSTITUTION'"))
        {
            throw problem(source.error());
        }
    }

    void start(mxq::ODBC& source, const std::vector<Table>& tables) override
    {
        std::ostringstream ss;
        ss << "LOCK TABLE "
           << mxb::transform_join(tables, [](const auto& t){
            return "`"s + t.schema() + "`.`" + t.table() + "` READ";
        }, ",");

        if (!source.query(ss.str()))
        {
            throw problem(source.error());
        }
    }

    void start_thread(mxq::ODBC& source, const std::vector<Table>& tables) override
    {
        if (!source.query("START TRANSACTION WITH CONSISTENT SNAPSHOT"))
        {
            throw problem(source.error());
        }
    }

    void threads_started(mxq::ODBC& source, const std::vector<Table>& tables) override
    {
        if (!source.query("UNLOCK TABLES"))
        {
            throw problem(source.error());
        }
    }

    std::string create_table(mxq::ODBC& source, const Table& table) override
    {
        std::string sql = mxb::string_printf("SHOW CREATE TABLE `%s`.`%s`", table.schema(), table.table());
        return field_from_result(source, sql, 1);
    }

    std::string select(mxq::ODBC& source, const Table& table) override
    {
        const char* format =
            R"(
SELECT
  'SELECT ' || GROUP_CONCAT('`' || COLUMN_NAME || '`' ORDER BY ORDINAL_POSITION SEPARATOR ',') ||
  ' FROM `' || TABLE_SCHEMA || '`.`' || TABLE_NAME || '`'
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = '%s' AND TABLE_NAME = '%s' AND IS_GENERATED = 'NEVER'
GROUP BY TABLE_SCHEMA, TABLE_NAME;
)";

        std::string sql = mxb::string_printf(format, table.schema(), table.table());
        return field_from_result(source, sql, 0);
    }

    std::string insert(mxq::ODBC& source, const Table& table) override
    {
        const char* format =
            R"(
SELECT
  'INSERT INTO `' || TABLE_SCHEMA || '`.`' || TABLE_NAME ||
  '` (' || GROUP_CONCAT('`' || COLUMN_NAME || '`' ORDER BY ORDINAL_POSITION SEPARATOR ',') ||
  ') VALUES (' || GROUP_CONCAT('?' SEPARATOR ',') || ')'
FROM INFORMATION_SCHEMA.COLUMNS
WHERE TABLE_SCHEMA = '%s' AND TABLE_NAME = '%s' AND IS_GENERATED = 'NEVER'
GROUP BY TABLE_SCHEMA, TABLE_NAME;
)";

        std::string sql = mxb::string_printf(format, table.schema(), table.table());
        return field_from_result(source, sql, 0);
    }
};

class PostgresqlExtractor final : public Extractor
{
public:
    void init_connection(mxq::ODBC& source) override
    {
        for (auto query : {
                "SET DATESTYLE = ISO",
                "SET INTERVALSTYLE = SQL_STANDARD",
                "SET statement_timeout = 0",
                "SET idle_in_transaction_session_timeout = 0",
                "SET lock_timeout = 0",
                "SET extra_float_digits = 3",
                "SET client_encoding = UTF8",
                "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"
            })
        {
            if (!source.query(query))
            {
                MXB_INFO("Query '%s' failed: %d, %s", query, source.errnum(), source.error().c_str());
                throw problem("Failed to prepare connection: ", source.error());
            }
        }
    }

    void start(mxq::ODBC& source, const std::vector<Table>& tables) override
    {
        for (const auto& t : tables)
        {
            auto sql = mxb::string_printf(R"(LOCK TABLE "%s"."%s" IN ACCESS SHARE MODE)",
                                          t.schema(), t.table());

            if (!source.query(sql))
            {
                throw problem("Failed to lock table `", t.schema(), "`.`", t.table(), "`: ", source.error());
            }
        }

        mxq::TextResult textresult;
        if (source.query("SELECT pg_export_snapshot()", &textresult))
        {
            if (auto val = textresult.get_field(0))
            {
                m_snapshot = *val;
            }
            else
            {
                throw problem("Transaction snapshot was null");
            }
        }
        else
        {
            throw problem("Failed to retrieve transaction snapshot: ", source.error());
        }
    }

    void start_thread(mxq::ODBC& source, const std::vector<Table>& tables) override
    {
        // Taking a shared lock on the tables prevents them from being deleted or modified while the
        // transaction is ongoing. The NOWAIT option is what prevents deadlocks from happening: if an outside
        // connection manages to request an exclusive lock after we get the initial shared locks but before
        // all of the threads have acquired their own locks, they would be blocked by the exclusive lock which
        // in turn would be blocked by the initial shared locks.
        for (const auto& t : tables)
        {
            auto sql = mxb::string_printf(R"(LOCK TABLE "%s"."%s" IN ACCESS SHARE MODE NOWAIT)",
                                          t.schema(), t.table());

            if (!source.query(sql))
            {
                throw problem("Locking conflict for table `", t.schema(), "`.`", t.table(), "`",
                              ", cannot proceed: ", source.error());
            }
        }

        if (!source.query("SET TRANSACTION SNAPSHOT '" + m_snapshot + "'"))
        {
            throw problem("Failed to import transaction snapshot: ", source.error());
        }
    }

    void threads_started(mxq::ODBC& source, const std::vector<Table>& tables) override
    {
        // Tables are unlocked when the transaction ends
    }

    std::string create_table(mxq::ODBC& source, const Table& table) override
    {
        /**
         * The various PostgreSQL types are converted as follows:
         *
         * - If a.attndims is larger than zero, then the field is an array and it is converted into a JSON
         *   array
         *
         * - The hstore key-value type is converted into a JSON object
         *
         * - JSONB is converted into JSON, MariaDB doesn't have a binary type
         *
         * - The PostgreSQL-only geometry types 'line', 'lseg', 'box' and 'circle' are converted to TEXT
         *
         * - The 'geometry' type is converted into a plain GEOMETRY type, Postgres otherwise declares it as a
         *   non-standard type.
         *
         * - The INET type is converted into INET6 as PostgreSQL supports a mix of IPv4 and IPv6 addresses.
         *
         * - All PostgreSQL SERIAL types are converted into MariaDB AUTO_INCREMENT fields. This isn't a 100%
         *   compatible mapping as PostgreSQL uses a SEQUENCE to implement it which allows multiple SERIAL
         *   fields to be defined and they do not need to be a part of the PRIMARY KEY to work. MariaDB
         *   requires that there is only one AUTO_INCREMENT field and it must be a part of the primary key.
         *
         * - Fields with with 'NULL::' as the starting of the expression mean that the field has a default
         *   value of NULL. This usually seems to happen when an explicit NULL default is used instead of an
         *   implicit one.
         *
         * - Fields that use a sequence for their default value are converted to use the MariaDB syntax.
         *
         * - If a.attgenerated is an empty string, then the field has a default value. Otherwise, the field is
         *   a generated column.
         *
         * - CHECK constrains are extracted as-is which means they must be compatible with MariaDB.
         */
        std::ostringstream ss;
        ss <<
            R"(
SELECT '`' || a.attname || '` ' ||
  CASE
    WHEN a.attndims > 0 THEN 'JSON'
    WHEN t.typname IN ('jsonb', 'json', 'hstore') THEN 'JSON'
    WHEN t.typname LIKE 'timestamp%' THEN 'TIMESTAMP'
    WHEN t.typname LIKE 'time%' THEN 'TIME'
    WHEN t.typname IN ('line', 'lseg', 'box', 'circle', 'cidr', 'macaddr', 'macaddr8') THEN 'TEXT'
    WHEN t.typname = 'geometry' THEN 'GEOMETRY'
    WHEN t.typname = 'inet' THEN 'INET6'
    ELSE UPPER(pg_catalog.format_type(a.atttypid, a.atttypmod))
  END ||
  CASE WHEN a.attnotnull THEN ' NOT NULL' ELSE '' END ||
  CASE
    WHEN pg_catalog.pg_get_serial_sequence(c.relname, a.attname) IS NOT NULL
      THEN ' AUTO_INCREMENT'
    WHEN pg_catalog.pg_get_expr(d.adbin, d.adrelid, true) LIKE 'NULL::%'
      THEN ' DEFAULT NULL'
    WHEN pg_catalog.pg_get_expr(d.adbin, d.adrelid, true) LIKE 'nextval(%'
      THEN ' DEFAULT ' || TRANSLATE(REPLACE(pg_catalog.pg_get_expr(d.adbin, d.adrelid, true), '::regclass', ''), '''', '')
    ELSE
      COALESCE(CASE WHEN a.attgenerated = '' THEN ' DEFAULT ' ELSE ' AS ' END || '(' || pg_catalog.pg_get_expr(d.adbin, d.adrelid, true) || ')', '')
  END  ||
  COALESCE(' ' || pg_catalog.pg_get_constraintdef(ct.oid), '')
  colcol
FROM pg_class c
  JOIN pg_namespace n ON (n.oid = c.relnamespace)
  JOIN pg_attribute a ON (a.attrelid = c.oid)
  JOIN pg_type t ON (t.oid = a.atttypid)
  LEFT JOIN pg_attrdef d ON (d.adrelid = a.attrelid AND d.adnum = a.attnum)
  LEFT JOIN pg_constraint ct ON (ct.conrelid = c.oid AND a.attnum = ANY(ct.conkey) AND ct.contype = 'c')
WHERE a.attnum > 0
AND n.nspname = ')" << table.schema() << R"('
AND c.relname = ')" << table.table() << R"('
ORDER BY a.attnum
)";
        std::string col_sql = ss.str();
        ss.str("");

        /**
         * PostgreSQL has many index types and the ones that MariaDB support are primary keys, unique keys,
         * normal indexes and spatial indexes. There doesn't seem to be built-in fulltext indexes but there
         * are some contributed modules. For now, ignore those and let the user deal with those.
         */
        ss <<
            R"(
SELECT
  CASE
    WHEN BOOL_OR(ix.indisprimary) THEN 'PRIMARY KEY'
    WHEN BOOL_OR(ix.indisunique) THEN 'UNIQUE KEY `' || i.relname || '`'
    ELSE 'KEY `' || i.relname || '`'
  END
  || '(' || STRING_AGG('`' || a.attname || '`', ', ' ORDER BY array_positions(ix.indkey, a.attnum)) || ')' idx
FROM pg_class t, pg_class i, pg_index ix, pg_attribute a, pg_namespace n
WHERE
  t.oid = ix.indrelid
  AND i.oid = ix.indexrelid
  AND n.oid = t.relnamespace
  AND a.attrelid = t.oid
  AND a.attnum = ANY(ix.indkey)
  AND t.relkind = 'r'
  AND n.nspname = ')" << table.schema() << R"('
  AND t.relname = ')" << table.table() << R"('
GROUP BY i.relname, t.relname
)";
        std::string idx_sql = ss.str();
        ss.str("");

        /**
         * PostgreSQL has a slightly different syntax when it comes to declaring foreign keys. They are of the
         * form `[CONSTRAINT name] FOREIGN KEY (fk_columns) REFERENCES (pk_columns)` and the output of
         * pg_get_constraintdef() never seems to contain the constraint name. We can map these into the
         * MariaDB form by manually adding the constraint and index names into the foreign key definition.
         * This is much easier than having to deal with the pg_constraint table and the arrays it uses
         * to define the field order.
         */
        ss <<
            R"(
SELECT 'CONSTRAINT `' || ct.conname || '`' ||
' FOREIGN KEY `' || (SELECT relname FROM pg_class WHERE oid = ct.conindid) || '` ' ||
REPLACE(pg_catalog.pg_get_constraintdef(ct.oid), 'FOREIGN KEY (', '(')
FROM pg_class t JOIN pg_constraint ct ON (t.oid = ct.conrelid)
JOIN pg_namespace n ON (t.relnamespace = n.oid)
WHERE
  ct.contype = 'f'
  AND n.nspname = ')" << table.schema() << R"('
  AND t.relname = ')" << table.table() << R"('
)";
        std::string fk_sql = ss.str();

        // Processing the results separately avoids the need to use CTEs and STRING_AGG to combine the fields.
        // It also allows us to format the result to look similar to SHOW CREATE TABLE.
        std::vector<std::string> values;

        for (auto sql : {col_sql, idx_sql, fk_sql})
        {
            mxq::TextResult textresult;

            if (source.query(sql, &textresult))
            {
                if (textresult.result().size() != 1)
                {
                    mxb_assert_message(!true, "Wrong number of results (%lu): %s",
                                       textresult.result().size(), sql.c_str());
                    throw problem("Unexpected number of results");
                }

                for (const auto& row : textresult.result()[0])
                {
                    if (row.size() != 1 || !row[0])
                    {
                        mxb_assert_message(!true, "Wrong number of results (%lu) or null row", row.size());
                        throw problem("Unexpected result value");
                    }

                    values.push_back(*row[0]);
                }
            }
            else
            {
                throw problem(source.error());
            }
        }

        ss.str("");
        ss << "CREATE TABLE `" << table.schema() << "`.`" << table.table() << "`(\n  ";
        ss << mxb::join(values, ",\n  ");
        ss << "\n)";
        return ss.str();
    }

    std::string select(mxq::ODBC& source, const Table& table) override
    {
        /**
         * The SELECT statement must also be generated based on the table layout and possibly also on the data
         * itself. The generated SQL is minimally formatted into a somewhat readable form. The following data
         * type conversions are done on the PostgreSQL side:
         *
         * - Geometry types are extracted into their WKT form, the native display format is something else.
         *
         * - hstore and array are read in their JSON form.
         *
         * - As the inet type in PostgreSQL is a mix of IPv4 and IPv6 addresses, we need to map IPv4 addresses
         *   into the IPv6 form. The inet type also stores a netmask which must be stripped off as MariaDB
         *   doesn't support them.
         */
        const char* format =
            R"(
SELECT
  E'SELECT\n' ||
  STRING_AGG(
    '  ' ||
    CASE
    WHEN data_type IN ('point', 'path', 'polygon', 'geometry')
      THEN 'ST_AsText(CAST(' || QUOTE_IDENT(column_name) || ' AS GEOMETRY)) ' || QUOTE_IDENT(column_name)
    WHEN udt_name IN ('hstore')
      THEN 'hstore_to_json_loose(' || QUOTE_IDENT(column_name) || ') ' || QUOTE_IDENT(column_name)
    WHEN data_type = 'array'
      THEN 'array_to_json(' || QUOTE_IDENT(column_name) || ') ' || QUOTE_IDENT(column_name)
    WHEN data_type = 'inet'
      THEN 'CASE FAMILY(' || QUOTE_IDENT(column_name) || ') WHEN 4 THEN ''::ffff:'' ELSE '''' END || HOST(' || QUOTE_IDENT(column_name) || ') ' || QUOTE_IDENT(column_name)
    ELSE
      QUOTE_IDENT(column_name)
    END
    , E',\n' ORDER BY ordinal_position) ||
  E'\nFROM ' || QUOTE_IDENT(table_schema) || '.' || QUOTE_IDENT(table_name)
FROM information_schema.columns
WHERE table_schema = '%s' AND table_name = '%s' AND is_generated = 'NEVER'
GROUP BY table_schema, table_name;
)";

        std::string sql = mxb::string_printf(format, table.schema(), table.table());
        return field_from_result(source, sql, 0);
    }

    std::string insert(mxq::ODBC& source, const Table& table) override
    {
        const char* format =
            R"(
SELECT
  'INSERT INTO `%s`.`%s` (' ||
  STRING_AGG( '`' || column_name || '`', ',' ORDER BY ordinal_position)
  || ') VALUES (' || STRING_AGG('?', ',') || ')'
FROM information_schema.columns
WHERE table_schema = '%s' AND table_name = '%s' AND is_generated = 'NEVER'
GROUP BY table_schema, table_name;
)";

        std::string sql = mxb::string_printf(format,
                                             table.schema(), table.table(),
                                             table.schema(), table.table());

        return field_from_result(source, sql, 0);
    }

private:
    std::string m_snapshot;
};

std::unique_ptr<ETL> create(const mxb::Json& json,
                            const HttpSql::ConnectionConfig& src_cc,
                            const HttpSql::ConnectionConfig& dest_cc)
{

    auto maybe_add = [](const std::string& keyword, const std::string& str){
        return str.empty() ? "" : keyword + "=" + str + ";";
    };

    if (src_cc.target != "odbc")
    {
        throw problem("Only ODBC targets are supported");
    }

    mxb_assert(ServerManager::find_by_unique_name(dest_cc.target));

    std::ostringstream ss;

    // We know what the library name is and it'll work regardless of the odbc.ini configuration.
    ss << "DRIVER=libmaodbc.so;";

    // If the user provided some extra options, put them first so that they take precedence over the ones
    // that are generated from the server configuration.
    if (auto extra = maybe_get(json, "connection_string", Type::STRING).get_string(); !extra.empty())
    {
        if (extra.back() != ';')
        {
            extra += ';';
        }

        ss << extra;
    }

    ss << "SERVER=" << dest_cc.host << ";"
       << "PORT=" << dest_cc.port << ";"
       << "UID=" << dest_cc.user << ";"
       << "PWD={" << dest_cc.password << "};"
       << "OPTION=67108864;";   // Enables multi-statment SQL.

    ss << maybe_add("DATABASE", dest_cc.db);

    if (dest_cc.ssl.enabled)
    {
        ss << maybe_add("SSLCERT", dest_cc.ssl.cert)
           << maybe_add("SSLKEY", dest_cc.ssl.key)
           << maybe_add("SSLCA", dest_cc.ssl.ca)
           << maybe_add("SSLCRL", dest_cc.ssl.crl)
           << maybe_add("SSLCIPHER", dest_cc.ssl.cipher);
    }

    auto type_str = get(json, "type", Type::STRING).get_string();
    std::unique_ptr<Extractor> extractor;

    std::string src = src_cc.odbc_string;

    if (type_str == "mariadb")
    {
        extractor = std::make_unique<MariaDBExtractor>();
    }
    else if (type_str == "postgresql")
    {
        // The Postgres ODBC driver by default wraps all SQL statements in their own SAVEPOINT commands in
        // order to be able to roll them back. We don't want them as they interfere with the
        // pg_export_snapshot() functionality. Postgres 7.4 implemented the protocol version 3 and the -0 at
        // the end of the Protocol option is what disables the SAVEPOINT functionality.
        src += ";Protocol=7.4-0";
        // It also emulates cursors by default which end up causing the whole resultset to be read
        // into memory. This would cause MaxScale to run out of memory so we need to use real cursors.
        src += ";UseDeclareFetch=1";
        extractor = std::make_unique<PostgresqlExtractor>();
    }
    else if (type_str == "generic")
    {
        extractor = std::make_unique<GenericExtractor>(get(json, "catalog", Type::STRING).get_string());
    }
    else
    {
        throw problem("Unknown value for 'type': ", type_str);
    }

    auto tables = get(json, "tables", Type::ARRAY).get_array_elems();

    if (tables.empty())
    {
        throw problem("No tables defined");
    }

    Config cnf{src, ss.str()};
    cnf.threads = std::min(16UL, tables.size());

    if (int64_t threads = maybe_get(json, "threads", mxb::Json::Type::INTEGER).get_int(); threads > 0)
    {
        cnf.threads = std::min((size_t)threads, tables.size());
    }

    std::unique_ptr<ETL> etl = std::make_unique<ETL>(cnf, std::move(extractor));

    for (const auto& val : tables)
    {
        etl->tables().emplace_back(*etl,
                                   get(val, "schema", Type::STRING).get_string(),
                                   get(val, "table", Type::STRING).get_string(),
                                   maybe_get(val, "create", Type::STRING).get_string(),
                                   maybe_get(val, "select", Type::STRING).get_string(),
                                   maybe_get(val, "insert", Type::STRING).get_string());
    }

    return etl;
}

Table::Table(ETL& etl,
             std::string_view schema,
             std::string_view table,
             std::string_view create,
             std::string_view select,
             std::string_view insert)
    : m_etl(etl)
    , m_schema(schema)
    , m_table(table)
    , m_create(create)
    , m_select(select)
    , m_insert(insert)
{
}

mxb::Json Table::to_json() const
{
    mxb::Json obj(mxb::Json::Type::OBJECT);
    obj.set_string("table", m_table);
    obj.set_string("schema", m_schema);

    if (!m_create.empty())
    {
        obj.set_string("create", m_create);
    }

    if (!m_select.empty())
    {
        obj.set_string("select", m_select);
    }

    if (!m_insert.empty())
    {
        obj.set_string("insert", m_insert);
    }

    if (!m_error.empty())
    {
        obj.set_string("error", m_error);
    }

    if (m_duration.count())
    {
        obj.set_float("execution_time", mxb::to_secs(m_duration));
    }

    return obj;
}

void Table::read_sql(mxq::ODBC& source)
{
    try
    {
        auto& extractor = m_etl.extractor();

        if (m_create.empty())
        {
            m_create = extractor.create_table(source, *this);
        }

        if (m_select.empty())
        {
            m_select = extractor.select(source, *this);
        }

        if (m_insert.empty())
        {
            m_insert = extractor.insert(source, *this);
        }
    }
    catch (const Error& e)
    {
        m_error = e.what();
        m_etl.add_error();
    }
}

void Table::create_objects(mxq::ODBC& source, mxq::ODBC& dest)
{
    try
    {
        mxb_assert(!m_create.empty());

        if (!dest.query("CREATE DATABASE IF NOT EXISTS `" + m_schema + "`"))
        {
            throw problem("Failed to create database: ", dest.error());
        }

        if (!dest.query("USE `" + m_schema + "`"))
        {
            throw problem("Failed to use database: ", dest.error());
        }

        if (!dest.query(m_create))
        {
            throw problem("Failed to create table: ", dest.error());
        }
    }
    catch (const Error& e)
    {
        m_error = e.what();
        m_etl.add_error();
    }
}

void Table::load_data(mxq::ODBC& source, mxq::ODBC& dest)
{
    try
    {
        mxb_assert(!m_select.empty() && !m_insert.empty());
        auto start = mxb::Clock::now();

        if (!source.prepare(m_select))
        {
            throw problem("Failed to prepare SELECT: ", source.error());
        }

        if (!dest.prepare(m_insert))
        {
            throw problem("Failed to prepare INSERT: ", dest.error());
        }

        if (!source.execute(dest.as_output()))
        {
            throw problem("Failed to load data: ", source.error(), dest.error());
        }

        auto end = mxb::Clock::now();
        m_duration = end - start;
    }
    catch (const Error& e)
    {
        m_error = e.what();
        m_etl.add_error();
    }
}

mxq::ODBC ETL::connect_to_source()
{
    mxq::ODBC source(config().src);

    if (!source.connect())
    {
        throw problem("Failed to connect to the source: ", source.error());
    }

    extractor().init_connection(source);
    extractor().start_thread(source, m_tables);
    return source;
}

std::pair<mxq::ODBC, mxq::ODBC> ETL::connect_to_both()
{
    mxq::ODBC source = connect_to_source();
    mxq::ODBC dest(config().dest);

    if (!dest.connect())
    {
        throw problem("Failed to connect to the destination: ", dest.error());
    }

    // Disabling UNIQUE_CHECKS, FOREIGN_KEY_CHECKS and AUTOCOMMIT will put InnoDB into a special mode
    // where inserting data is more efficient than it normally would be if the table is empty.
    auto SQL_SETUP = "SET MAX_STATEMENT_TIME=0, "
                     "SQL_MODE='ANSI_QUOTES,PIPES_AS_CONCAT,NO_ENGINE_SUBSTITUTION', "
                     "UNIQUE_CHECKS=0, FOREIGN_KEY_CHECKS=0, AUTOCOMMIT=0, SQL_NOTES=0";

    if (!dest.query(SQL_SETUP))
    {
        throw problem("Failed to setup connection: ", dest.error());
    }

    return {std::move(source), std::move(dest)};
}

Table* ETL::next_table()
{
    mxb_assert(!m_tables.empty());
    Table* rval = nullptr;

    if (size_t offset = m_counter.fetch_add(1, std::memory_order_relaxed); offset < m_tables.size())
    {
        rval = &m_tables[offset];
    }

    return rval;
}

bool ETL::checkpoint(int* current_checkpoint)
{
    std::unique_lock guard(m_lock);

    if (*current_checkpoint == m_next_checkpoint)
    {
        // This is the first thread to arrive at the latest checkpoint. Reset the table counter to start
        // iteration from the beginning.
        ++m_next_checkpoint;
        m_counter.store(0, std::memory_order_relaxed);
    }

    ++(*current_checkpoint);

    return !m_have_error;
}

void ETL::add_error()
{
    std::unique_lock guard(m_lock);
    m_have_error = true;
}

template<auto connect_func, auto run_func>
mxb::Json ETL::run_job()
{
    std::string error;

    try
    {
        mxq::ODBC coordinator(m_config.src);

        if (!coordinator.connect())
        {
            throw problem(coordinator.error());
        }

        extractor().init_connection(coordinator);
        extractor().start(coordinator, m_tables);

        using Connection = decltype(std::invoke(connect_func, this));
        std::vector<Connection> connections;

        for (size_t i = 0; i < m_config.threads; i++)
        {
            connections.emplace_back(std::invoke(connect_func, this));
        }

        mxb_assert(connections.size() <= m_tables.size());

        extractor().threads_started(coordinator, m_tables);

        std::vector<std::thread> threads;

        for (auto& c : connections)
        {
            threads.emplace_back(run_func, this, std::ref(c));
        }

        std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    }
    catch (const Error& e)
    {
        error = e.what();
    }

    mxb::Json rval(mxb::Json::Type::OBJECT);
    mxb::Json arr(mxb::Json::Type::ARRAY);
    bool ok = !m_have_error;

    for (const auto& t : m_tables)
    {
        arr.add_array_elem(t.to_json());
    }

    if (!error.empty())
    {
        ok = false;
        rval.set_string("error", error);
    }

    rval.set_bool("ok", ok);
    rval.set_object("tables", std::move(arr));

    return rval;
}

void ETL::run_prepare_job(mxq::ODBC& source) noexcept
{
    while (auto t = next_table())
    {
        t->read_sql(source);
    }
}

void ETL::run_start_job(std::pair<mxq::ODBC, mxq::ODBC>& connections) noexcept
{
    auto& [source, dest] = connections;
    int my_checkpoint = 0;

    while (auto t = next_table())
    {
        t->read_sql(source);
    }

    m_init_latch.arrive_and_wait();

    if (checkpoint(&my_checkpoint))
    {
        while (auto t = next_table())
        {
            t->create_objects(source, dest);
        }

        m_create_latch.arrive_and_wait();

        if (checkpoint(&my_checkpoint))
        {
            while (auto t = next_table())
            {
                t->load_data(source, dest);
            }
        }
    }
}

mxb::Json ETL::prepare()
{
    return run_job<&ETL::connect_to_source, &ETL::run_prepare_job>();
}

mxb::Json ETL::start()
{
    return run_job<&ETL::connect_to_both, &ETL::run_start_job>();
}
}
